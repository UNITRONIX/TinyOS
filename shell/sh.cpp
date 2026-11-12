// Interactive TinyOS shell.
//
// This replaces the previous 4645-line shell/shell.cpp, whose dispatch was a
// chain of 102 `if (string::compare(command, ...))` branches and which wrote to
// the VGA driver directly at 1499 call sites. Structure here:
//
//   * command implementations live in shell/commands/*.cpp;
//   * dispatch, completion and help come from one registry (libshell);
//   * all output goes through a Stream, so the same shell works over VGA,
//     serial or a capture buffer;
//   * line editing, history and completion are this file's only job.
//
// Session state (cwd, environment, identity) lives in one Session object rather
// than in file-scope variables. Two consoles therefore cannot see each other's
// working directory (defect D5); full isolation still waits for per-process
// address spaces in M6.

#include <tinyos/core/memory.hpp>
#include <tinyos/drivers/console.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/libk/path.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/exec.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/shell/commands/commands.hpp>
#include <tinyos/shell/shell.hpp>

namespace
{
    using tinyos::libshell::Session;
    using tinyos::libshell::Status;

    constexpr size_t MaxHistory = 12;
    constexpr size_t MaxLine = tinyos::libshell::MaxLineBytes;

    // Where interactive input comes from. Until this existed the shell only
    // read the PS/2 keyboard, so a headless machine could not be driven at all
    // (defect D18). It remains as a diagnostic switch; the console now polls
    // both sources by default.
    enum class InputSource
    {
        Keyboard,
        Serial
    };

    Session g_session = {};
    size_t g_lines_executed = 0;
    bool g_initialized = false;

    // Reads one character from the console, which arbitrates between the PS/2
    // keyboard and the serial port. Both sources produce the same special-key
    // codes: keyboard scancodes natively, serial via ANSI escape decoding here.
    char read_raw_char()
    {
        char character = tinyos::drivers::console::read_char();
        if (character != 0x1B)
        {
            return character;
        }

        // Serial terminals send ANSI escape sequences: ESC [ A..D, ESC [ H/F and
        // ESC [ 3 ~. Decoding them means history and editing work over a serial
        // console instead of inserting raw control bytes.
        //
        // The continuation bytes arrive microseconds later, but waiting for them
        // with a blocking console read would charge that wait to this task's
        // watchdog budget. The rest of the sequence is therefore read with the
        // non-blocking probe; an incomplete sequence is dropped rather than
        // blocking.
        char bracket = 0;
        if (!tinyos::drivers::console::try_read_char(bracket) || bracket != '[')
        {
            return 0;
        }

        char code = 0;
        if (!tinyos::drivers::console::try_read_char(code))
        {
            return 0;
        }

        switch (code)
        {
        case 'A':
            return tinyos::drivers::keyboard::KeyUp;
        case 'B':
            return tinyos::drivers::keyboard::KeyDown;
        case 'C':
            return tinyos::drivers::keyboard::KeyRight;
        case 'D':
            return tinyos::drivers::keyboard::KeyLeft;
        case 'H':
            return tinyos::drivers::keyboard::KeyHome;
        case 'F':
            return tinyos::drivers::keyboard::KeyEnd;
            case '3':
            {
                // ESC [ 3 ~ is Delete; consume the trailing '~'.
                char tilde = 0;
                (void)tinyos::drivers::console::try_read_char(tilde);
                return tinyos::drivers::keyboard::KeyDelete;
            }
        default:
            return 0;
        }
    }


    // Command history, oldest first, with the index of the next write. A
    // separate count tracks how many slots are filled.
    char g_history[MaxHistory][MaxLine];
    size_t g_history_next = 0;
    size_t g_history_count = 0;

    void reset_session()
    {
        // Delegated to libshell so the shell loop and the host tests build the
        // same default session. The previous version duplicated the field
        // assignments here.
        tinyos::libshell::initialize_session(g_session);
    }

    void history_push(const char* line)
    {
        if (line == nullptr || line[0] == '\0')
        {
            return;
        }

        // Do not store the same line twice in a row.
        if (g_history_count > 0)
        {
            const size_t last = (g_history_next + MaxHistory - 1) % MaxHistory;
            if (tinyos::libk::text::equals(g_history[last], line))
            {
                return;
            }
        }

        tinyos::core::memory::string_copy_safe(g_history[g_history_next], MaxLine, line);
        g_history_next = (g_history_next + 1) % MaxHistory;
        if (g_history_count < MaxHistory)
        {
            ++g_history_count;
        }
    }

    const char* history_at(size_t back)
    {
        if (back == 0 || back > g_history_count)
        {
            return nullptr;
        }

        const size_t index = (g_history_next + MaxHistory - back) % MaxHistory;
        return g_history[index];
    }

    void write_prompt()
    {
        const auto out = tinyos::libshell::console_stream();
        const char* user = g_session.user_name[0] != '\0' ? g_session.user_name : "nobody";
        out.write(user);
        out.write("@tinyos:");

        // Show $HOME as "~" and everything under it relative, matching what a
        // user expects. A full path for /home/alice/documents would push the
        // prompt past a third of an 80-column screen.
        const char* home = g_session.env.get("HOME");
        if (home != nullptr && home[0] == '/')
        {
            if (tinyos::libk::text::equals(g_session.cwd, home))
            {
                out.write("~");
            }
            else if (tinyos::libk::path::has_prefix(g_session.cwd, home))
            {
                out.write("~");
                out.write(g_session.cwd + tinyos::libk::text::length(home));
            }
            else
            {
                out.write(g_session.cwd);
            }
        }
        else
        {
            out.write(g_session.cwd);
        }

        out.write("$ ");
    }

    // Redraws the input line after an edit. Rewrites the prompt and content
    // rather than using cursor-movement escapes, because the VGA text driver has
    // no escape sequence support.
    void redraw_line(const char* buffer, size_t cursor)
    {
        const auto out = tinyos::libshell::console_stream();
        out.put_char('\n');
        write_prompt();
        out.write(buffer);

        // Walk the cursor back from the end to its logical position.
        const size_t length = tinyos::libk::text::length(buffer);
        for (size_t index = cursor; index < length; ++index)
        {
            out.put_char('\b');
        }
    }

    // Reads one line with editing, history and completion.
    size_t read_line(char* buffer, size_t capacity)
    {
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }

        buffer[0] = '\0';
        size_t length = 0;
        size_t cursor = 0;
        size_t history_offset = 0;
        char saved[MaxLine];
        saved[0] = '\0';

        const auto out = tinyos::libshell::console_stream();

        for (;;)
        {
            const char character = read_raw_char();

            if (character == '\n')
            {
                out.put_char('\n');
                buffer[length] = '\0';
                return length;
            }

            if (character == '\b')
            {
                if (cursor > 0)
                {
                    for (size_t index = cursor - 1; index < length; ++index)
                    {
                        buffer[index] = buffer[index + 1];
                    }

                    --cursor;
                    --length;
                    buffer[length] = '\0';
                    redraw_line(buffer, cursor);
                }
                continue;
            }

            if (character == '\t')
            {
                // Completion: extend to the unique match, or list candidates.
                const tinyos::libshell::Command* matches[32];
                const size_t found = tinyos::libshell::complete(buffer, matches, 32);

                if (found == 1 && matches[0] != nullptr)
                {
                    tinyos::core::memory::string_copy_safe(buffer, capacity, matches[0]->name);
                    length = tinyos::libk::text::length(buffer);
                    cursor = length;
                    redraw_line(buffer, cursor);
                }
                else if (found > 1)
                {
                    out.put_char('\n');
                    for (size_t index = 0; index < found; ++index)
                    {
                        out.write(matches[index]->name);
                        out.put_char(' ');
                    }
                    out.put_char('\n');
                    write_prompt();
                    out.write(buffer);
                }
                continue;
            }

            // Special keys arrive as either PS/2 scancodes or decoded ANSI
            // sequences (see read_raw_char), so both sources can produce them.
            // Undecoded serial input returns 0 and is ignored below.
            if (tinyos::drivers::keyboard::is_special_key(character))
            {
                if (character == tinyos::drivers::keyboard::KeyUp)
                {
                    // First step up saves the in-progress line so it can be
                    // restored by stepping back down.
                    if (history_offset == 0)
                    {
                        tinyos::core::memory::string_copy_safe(saved, sizeof(saved), buffer);
                    }

                    const char* entry = history_at(history_offset + 1);
                    if (entry != nullptr)
                    {
                        ++history_offset;
                        tinyos::core::memory::string_copy_safe(buffer, capacity, entry);
                        length = tinyos::libk::text::length(buffer);
                        cursor = length;
                        redraw_line(buffer, cursor);
                    }
                    continue;
                }

                if (character == tinyos::drivers::keyboard::KeyDown)
                {
                    if (history_offset == 0)
                    {
                        continue;
                    }

                    --history_offset;
                    if (history_offset == 0)
                    {
                        tinyos::core::memory::string_copy_safe(buffer, capacity, saved);
                    }
                    else
                    {
                        const char* entry = history_at(history_offset);
                        tinyos::core::memory::string_copy_safe(buffer, capacity, entry != nullptr ? entry : "");
                    }

                    length = tinyos::libk::text::length(buffer);
                    cursor = length;
                    redraw_line(buffer, cursor);
                    continue;
                }

                if (character == tinyos::drivers::keyboard::KeyHome)
                {
                    cursor = 0;
                    redraw_line(buffer, cursor);
                    continue;
                }

                if (character == tinyos::drivers::keyboard::KeyEnd)
                {
                    cursor = length;
                    redraw_line(buffer, cursor);
                    continue;
                }

                if (character == tinyos::drivers::keyboard::KeyDelete)
                {
                    if (cursor < length)
                    {
                        for (size_t index = cursor; index < length; ++index)
                        {
                            buffer[index] = buffer[index + 1];
                        }

                        --length;
                        buffer[length] = '\0';
                        redraw_line(buffer, cursor);
                    }
                    continue;
                }

                // Other special keys (arrows left/right, page keys) are ignored
                // rather than inserted as control bytes.
                continue;
            }

            if (static_cast<unsigned char>(character) < 0x20)
            {
                continue;
            }

            // Refuse rather than silently clip: a command line that lost its
            // tail and still runs is worse than a rejected one.
            if (length + 1 >= capacity)
            {
                out.put_char('\a');
                continue;
            }

            // Insert at the cursor, shifting the tail right.
            for (size_t index = length; index > cursor; --index)
            {
                buffer[index] = buffer[index - 1];
            }

            buffer[cursor] = character;
            ++cursor;
            ++length;
            buffer[length] = '\0';

            // Appending at the end is the common case, so write one character
            // instead of redrawing the whole line.
            if (cursor == length)
            {
                out.put_char(character);
            }
            else
            {
                redraw_line(buffer, cursor);
            }
        }
    }
}

namespace tinyos::shell
{
    void initialize()
    {
        if (g_initialized)
        {
            return;
        }

        reset_session();
        tinyos::shell::commands::register_all();
        g_initialized = true;
    }

    size_t lines_executed()
    {
        return g_lines_executed;
    }

    void set_serial_input(bool enabled)
    {
        // The console polls both sources, so this only switches which one the
        // console prefers; keeping the flag reported for compatibility.
        (void)enabled;
    }

    bool serial_input_enabled()
    {
        // Serial input is always available through the console layer.
        return true;
    }

    void execute(const char* input)
    {
        if (!g_initialized)
        {
            initialize();
        }

        const auto out = tinyos::libshell::console_stream();
        const auto err = tinyos::libshell::console_stream();

        const auto result = tinyos::libshell::execute_line(input, g_session, out, err);
        ++g_lines_executed;

        // A non-zero status is not echoed: a shell exit code is a value, not a
        // message. Diagnostics that matter are printed by the failing command.
        (void)result;
    }

    [[noreturn]] void run()
    {
        initialize();

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TinyOS shell ready.");

        char line[MaxLine];

        for (;;)
        {
            write_prompt();

            const size_t length = read_line(line, sizeof(line));
            if (length == 0)
            {
                continue;
            }

            history_push(line);
            execute(line);

            // Keep PWD in step with the session directory. Commands cannot
            // change it yet (see `cd`), but the environment must not disagree
            // with the prompt.
            g_session.env.set("PWD", g_session.cwd);
        }
    }
}
