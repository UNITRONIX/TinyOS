#include <stdint.h>

#include <tinyos/arch/io.hpp>
#include <tinyos/core/memory.hpp>
#include <tinyos/drivers/input.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>

namespace
{
    constexpr uint16_t StatusPort = 0x64;
    constexpr uint16_t DataPort = 0x60;
    constexpr size_t BufferSize = 128;
    constexpr uint8_t OutputBufferFull = 0x01;
    constexpr uint8_t TimeoutError = 0x40;
    constexpr uint8_t ParityError = 0x80;
    constexpr uint8_t AuxiliaryData = 0x20;
    constexpr uint8_t ExtendedPrefix0 = 0xE0;
    constexpr uint8_t ExtendedPrefix1 = 0xE1;

    constexpr char KeyMap[128] = {
        0,
        27,
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
        '\b',
        '\t',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
        '\n',
        0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0,
        '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
        0,
        '*',
        0,
        ' ',
        0
    };

    volatile char g_buffer[BufferSize] = {};
    volatile size_t g_read_index = 0;
    volatile size_t g_write_index = 0;
    volatile bool g_interrupt_input_enabled = false;
    bool g_extended_scancode = false;
    volatile uint64_t g_irq_scancode_count = 0;
    volatile uint64_t g_polled_scancode_count = 0;
    volatile uint64_t g_ignored_scancode_count = 0;
    volatile uint64_t g_dropped_character_count = 0;
    volatile bool g_has_seen_scancode = false;
    volatile uint8_t g_last_scancode = 0;
    bool g_shift_pressed = false;

    void debug_scancode(uint8_t scancode)
    {
#if defined(TINYOS_DEBUG_BOOT)
        constexpr char Digits[] = "0123456789ABCDEF";
        char text[] = "kbd 0x00";
        text[6] = Digits[(scancode >> 4) & 0x0F];
        text[7] = Digits[scancode & 0x0F];
        tinyos::drivers::serial::write("[debug-boot] ");
        tinyos::drivers::serial::write_line(text);
#else
        (void)scancode;
#endif
    }

    void flush_controller_output()
    {
        for (size_t attempt = 0; attempt < 32; ++attempt)
        {
            const uint8_t status = tinyos::arch::io::inb(StatusPort);
            if ((status & OutputBufferFull) == 0)
            {
                return;
            }

            const uint8_t discarded = tinyos::arch::io::inb(DataPort);
            debug_scancode(discarded);
        }
    }

    bool buffer_empty()
    {
        return g_read_index == g_write_index;
    }

    bool buffer_full()
    {
        return ((g_write_index + 1) % BufferSize) == g_read_index;
    }

    void push_character(char character)
    {
        if (character == 0 || buffer_full())
        {
            if (character != 0)
            {
                ++g_dropped_character_count;
            }
            return;
        }

        g_buffer[g_write_index] = character;
        g_write_index = (g_write_index + 1) % BufferSize;
    }

    bool read_scancode(uint8_t& scancode)
    {
        const uint8_t status = tinyos::arch::io::inb(StatusPort);
        if ((status & OutputBufferFull) == 0)
        {
            return false;
        }

        scancode = tinyos::arch::io::inb(DataPort);
        g_last_scancode = scancode;
        g_has_seen_scancode = true;

        if ((status & (TimeoutError | ParityError | AuxiliaryData)) != 0)
        {
            ++g_ignored_scancode_count;
            return false;
        }

        return true;
    }

    bool translate_scancode(uint8_t scancode, char& character)
    {
        character = 0;

        if (scancode == ExtendedPrefix0 || scancode == ExtendedPrefix1)
        {
            g_extended_scancode = true;
            return false;
        }

        if (g_extended_scancode)
        {
            g_extended_scancode = false;
            const bool released = (scancode & 0x80) != 0;
            const uint8_t make = static_cast<uint8_t>(scancode & 0x7F);
            if (released)
            {
                ++g_ignored_scancode_count;
                return false;
            }

            switch (make)
            {
            case 0x4B:
                character = tinyos::drivers::keyboard::KeyLeft;
                return true;
            case 0x4D:
                character = tinyos::drivers::keyboard::KeyRight;
                return true;
            case 0x48:
                character = tinyos::drivers::keyboard::KeyUp;
                return true;
            case 0x50:
                character = tinyos::drivers::keyboard::KeyDown;
                return true;
            case 0x47:
                character = tinyos::drivers::keyboard::KeyHome;
                return true;
            case 0x4F:
                character = tinyos::drivers::keyboard::KeyEnd;
                return true;
            case 0x53:
                character = tinyos::drivers::keyboard::KeyDelete;
                return true;
            case 0x49:
                character = tinyos::drivers::keyboard::KeyPgUp;
                return true;
            case 0x51:
                character = tinyos::drivers::keyboard::KeyPgDn;
                return true;
            default:
                ++g_ignored_scancode_count;
                return false;
            }
        }

        if (scancode == 0x4B || scancode == 0x4D || scancode == 0x48 || scancode == 0x50)
        {
            switch (scancode)
            {
            case 0x4B:
                character = tinyos::drivers::keyboard::KeyLeft;
                return true;
            case 0x4D:
                character = tinyos::drivers::keyboard::KeyRight;
                return true;
            case 0x48:
                character = tinyos::drivers::keyboard::KeyUp;
                return true;
            case 0x50:
                character = tinyos::drivers::keyboard::KeyDown;
                return true;
            }

            return false;
        }

        if ((scancode & 0x80) != 0)
        {
            const uint8_t release = static_cast<uint8_t>(scancode & 0x7F);
            if (release == 0x2A || release == 0x36)
            {
                g_shift_pressed = false;
            }

            ++g_ignored_scancode_count;
            return false;
        }

        if (scancode == 0x2A || scancode == 0x36)
        {
            g_shift_pressed = true;
            ++g_ignored_scancode_count;
            return false;
        }

        if (scancode >= sizeof(KeyMap) / sizeof(KeyMap[0]))
        {
            ++g_ignored_scancode_count;
            return false;
        }

        character = KeyMap[scancode];
        if (character == 0)
        {
            ++g_ignored_scancode_count;
            return false;
        }

        if (character == '\t' && g_shift_pressed)
        {
            character = tinyos::drivers::keyboard::KeyShiftTab;
        }

        return true;
    }

    bool try_poll_character(char& character)
    {
        uint8_t scancode = 0;
        if (!read_scancode(scancode))
        {
            return false;
        }

        ++g_polled_scancode_count;
        debug_scancode(scancode);

        if (!translate_scancode(scancode, character))
        {
            return false;
        }

        tinyos::drivers::input::push_key_event(character, true);
        return true;
    }

    bool pop_character(char& character)
    {
        if (buffer_empty())
        {
            return false;
        }

        character = g_buffer[g_read_index];
        g_read_index = (g_read_index + 1) % BufferSize;
        return true;
    }
}

namespace tinyos::drivers::keyboard
{
    void initialize()
    {
        g_read_index = 0;
        g_write_index = 0;
        g_interrupt_input_enabled = false;
        g_extended_scancode = false;
        g_irq_scancode_count = 0;
        g_polled_scancode_count = 0;
        g_ignored_scancode_count = 0;
        g_dropped_character_count = 0;
        g_has_seen_scancode = false;
        g_last_scancode = 0;
        flush_controller_output();
    }

    void enable_interrupt_input()
    {
        g_interrupt_input_enabled = true;
    }

    bool interrupt_input_enabled()
    {
        return g_interrupt_input_enabled;
    }

    char poll_character()
    {
        for (;;)
        {
            char character = 0;
            if (try_poll_character(character))
            {
                return character;
            }

            asm volatile ("pause");
        }
    }

    void handle_irq()
    {
        uint8_t scancode = 0;
        if (!read_scancode(scancode))
        {
            return;
        }

        ++g_irq_scancode_count;
        debug_scancode(scancode);

        char character = 0;
        if (!translate_scancode(scancode, character))
        {
            return;
        }

        push_character(character);
        tinyos::drivers::input::push_key_event(character, true);
    }

    char read_char()
    {
        for (;;)
        {
            char character;
            if (pop_character(character))
            {
                return character;
            }

            if (!g_interrupt_input_enabled)
            {
                return poll_character();
            }

            if (try_poll_character(character))
            {
                return character;
            }

            asm volatile ("hlt");
            tinyos::kernel::sched::poll_reschedule();
        }
    }

    void read_line(char* buffer, size_t max_length)
    {
        if (max_length == 0)
        {
            return;
        }

        size_t index = 0;
        core::memory::set(buffer, 0, max_length);

        for (;;)
        {
            const char character = read_char();

            if (character == '\n')
            {
                buffer[index] = '\0';
                tinyos::drivers::vga::put_char('\n');
                return;
            }

            if (character == '\b')
            {
                if (index > 0)
                {
                    --index;
                    core::memory::set(&buffer[index], 0, 1);
                    tinyos::drivers::vga::put_char('\b');
                }

                continue;
            }

            if (is_special_key(character))
            {
                continue;
            }

            if (!core::memory::buffer_check(max_length, index + 2))
            {
                continue;
            }

            buffer[index] = character;
            ++index;
            buffer[index] = '\0';
            tinyos::drivers::vga::put_char(character);
        }
    }

    bool is_special_key(char character)
    {
        return character == KeyLeft || character == KeyRight || character == KeyUp || character == KeyDown;
    }

    size_t buffered_character_count()
    {
        if (g_write_index >= g_read_index)
        {
            return g_write_index - g_read_index;
        }

        return BufferSize - g_read_index + g_write_index;
    }

    uint64_t irq_scancode_count()
    {
        return g_irq_scancode_count;
    }

    uint64_t polled_scancode_count()
    {
        return g_polled_scancode_count;
    }

    uint64_t ignored_scancode_count()
    {
        return g_ignored_scancode_count;
    }

    uint64_t dropped_character_count()
    {
        return g_dropped_character_count;
    }

    bool has_seen_scancode()
    {
        return g_has_seen_scancode;
    }

    uint8_t last_scancode()
    {
        return g_last_scancode;
    }
}
