#include <tinyos/drivers/console.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>

namespace
{
    tinyos::drivers::console::Backend g_backend = tinyos::drivers::console::Backend::Vga;

    // Mirrored by default. The previous build declared the flag and the setter
    // but never enabled it, so a headless run produced no console output even
    // though serial logging worked (defect D18).
    bool g_serial_mirror = true;

    tinyos::drivers::console::InputSource g_last_input =
        tinyos::drivers::console::InputSource::None;
    volatile uint64_t g_keyboard_input = 0;
    volatile uint64_t g_serial_input = 0;

    void mirror_char(char character)
    {
        if (g_serial_mirror)
        {
            tinyos::drivers::serial::write_char(character);
        }
    }
}

namespace tinyos::drivers::console
{
    void initialize()
    {
        g_backend = Backend::Vga;
        g_serial_mirror = true;
        g_last_input = InputSource::None;
        g_keyboard_input = 0;
        g_serial_input = 0;
    }

    Backend active_backend()
    {
        return g_backend;
    }

    void set_backend(Backend backend)
    {
        g_backend = backend;
    }

    void set_serial_mirror(bool enabled)
    {
        g_serial_mirror = enabled;
    }

    bool serial_mirror_enabled()
    {
        return g_serial_mirror;
    }

    void put_char(char character)
    {
        if (g_backend == Backend::Serial)
        {
            tinyos::drivers::serial::write_char(character);
            return;
        }

        mirror_char(character);
        tinyos::drivers::vga::hardware_put_char(character);
    }

    void write(const char* text)
    {
        if (text == nullptr)
        {
            return;
        }

        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            put_char(text[index]);
        }
    }

    void write_line(const char* text)
    {
        write(text);
        put_char('\n');
    }

    bool try_read_char(char& character)
    {
        // Keyboard first: on a desktop it is the expected source, and polling it
        // is cheap. The serial port then covers headless and scripted runs.
        char keyboard_character = 0;
        if (tinyos::drivers::keyboard::try_read_char(keyboard_character))
        {
            character = keyboard_character;
            g_last_input = InputSource::Keyboard;
            ++g_keyboard_input;
            return true;
        }

        char serial_character = 0;
        if (tinyos::drivers::serial::try_read_char(serial_character))
        {
            character = serial_character;
            g_last_input = InputSource::Serial;
            ++g_serial_input;
            return true;
        }

        return false;
    }

    char read_char()
    {
        // Declared once for the whole wait rather than per iteration: the task is
        // idle from the first failed poll until a character actually arrives.
        tinyos::kernel::sched::begin_idle_wait();

        for (;;)
        {
            char character = 0;
            if (try_read_char(character))
            {
                tinyos::kernel::sched::end_idle_wait();
                return character;
            }

            // `hlt` sleeps until the next interrupt; the PIT guarantees one
            // arrives within a tick, so a key that lands between the poll and the
            // halt is picked up on the next pass instead of hanging.
            tinyos::kernel::sched::poll_reschedule();
            asm volatile ("hlt");
        }
    }

    InputSource last_input_source()
    {
        return g_last_input;
    }

    uint64_t keyboard_input_count()
    {
        return g_keyboard_input;
    }

    uint64_t serial_input_count()
    {
        return g_serial_input;
    }

    bool validation_self_test()
    {
        // The backend must be one this build actually supports, and the mirror
        // must be on by default so a headless run is not silent.
        return active_backend() == Backend::Vga && serial_mirror_enabled();
    }
}
