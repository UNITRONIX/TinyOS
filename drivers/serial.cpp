#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/serial.hpp>

namespace
{
    constexpr unsigned short SerialPort = 0x3F8;
    bool g_initialized = false;
    unsigned long g_dropped_input = 0;

    bool transmitter_empty()
    {
        return (tinyos::arch::io::inb(SerialPort + 5) & 0x20) != 0;
    }

    bool receiver_ready()
    {
        return (tinyos::arch::io::inb(SerialPort + 5) & 0x01) != 0;
    }
}

namespace tinyos::drivers::serial
{
    void initialize()
    {
        arch::io::outb(SerialPort + 1, 0x00);
        arch::io::outb(SerialPort + 3, 0x80);
        arch::io::outb(SerialPort + 0, 0x03);
        arch::io::outb(SerialPort + 1, 0x00);
        arch::io::outb(SerialPort + 3, 0x03);
        arch::io::outb(SerialPort + 2, 0xC7);
        arch::io::outb(SerialPort + 4, 0x0B);
        g_initialized = true;
        g_dropped_input = 0;
    }

    void write_char(char character)
    {
        if (!g_initialized)
        {
            return;
        }

        if (character == '\n')
        {
            write_char('\r');
        }

        while (!transmitter_empty())
        {
        }

        arch::io::outb(SerialPort, static_cast<unsigned char>(character));
    }

    void write(const char* text)
    {
        for (unsigned int index = 0; text[index] != '\0'; ++index)
        {
            write_char(text[index]);
        }
    }

    void write_line(const char* text)
    {
        write(text);
        write_char('\n');
    }

    bool try_read_char(char& character)
    {
        if (!g_initialized || !receiver_ready())
        {
            return false;
        }

        const auto value = static_cast<char>(arch::io::inb(SerialPort));

        // Terminal emulators and QEMU send CR for Enter. Normalise to LF so the
        // shell has one line terminator to handle.
        character = value == '\r' ? '\n' : value;
        return true;
    }

    char read_char()
    {
        for (;;)
        {
            char character = 0;
            if (try_read_char(character))
            {
                return character;
            }

            // Nothing to do until a byte arrives. `hlt` also lets the scheduler
            // run other work instead of spinning on the port.
            asm volatile ("hlt");
        }
    }

    unsigned long dropped_input_count()
    {
        return g_dropped_input;
    }
}
