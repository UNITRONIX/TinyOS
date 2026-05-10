#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/serial.hpp>

namespace
{
    constexpr unsigned short SerialPort = 0x3F8;
    bool g_initialized = false;

    bool transmitter_empty()
    {
        return (tinyos::arch::io::inb(SerialPort + 5) & 0x20) != 0;
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
}
