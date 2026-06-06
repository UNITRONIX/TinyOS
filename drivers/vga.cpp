#include <stddef.h>
#include <stdint.h>

#include <tinyos/drivers/console.hpp>
#include <tinyos/drivers/vga.hpp>

namespace
{
    constexpr size_t VgaWidth = 80;
    constexpr size_t VgaHeight = 25;
    volatile uint16_t* const VgaBuffer = reinterpret_cast<uint16_t*>(0xB8000);

    size_t g_row = 0;
    size_t g_column = 0;
    uint8_t g_color = 0x07;

    constexpr uint8_t make_color(tinyos::drivers::vga::Color foreground, tinyos::drivers::vga::Color background)
    {
        return static_cast<uint8_t>(foreground) | (static_cast<uint8_t>(background) << 4);
    }

    constexpr uint16_t make_entry(char character, uint8_t color)
    {
        return static_cast<uint16_t>(static_cast<uint8_t>(character)) | (static_cast<uint16_t>(color) << 8);
    }

    void clear_row(size_t row)
    {
        for (size_t column = 0; column < VgaWidth; ++column)
        {
            VgaBuffer[row * VgaWidth + column] = make_entry(' ', g_color);
        }
    }

    void scroll_if_needed()
    {
        if (g_row < VgaHeight)
        {
            return;
        }

        for (size_t row = 1; row < VgaHeight; ++row)
        {
            for (size_t column = 0; column < VgaWidth; ++column)
            {
                VgaBuffer[(row - 1) * VgaWidth + column] = VgaBuffer[row * VgaWidth + column];
            }
        }

        clear_row(VgaHeight - 1);
        g_row = VgaHeight - 1;
    }

    void advance_line()
    {
        g_column = 0;
        ++g_row;
        scroll_if_needed();
    }
}

namespace tinyos::drivers::vga
{
    void initialize()
    {
        set_color(Color::LightGrey, Color::Black);
        clear();
    }

    void clear()
    {
        g_row = 0;
        g_column = 0;

        for (size_t row = 0; row < VgaHeight; ++row)
        {
            clear_row(row);
        }
    }

    void set_color(Color foreground, Color background)
    {
        g_color = make_color(foreground, background);
    }

    void hardware_put_char(char character)
    {
        if (character == '\n')
        {
            advance_line();
            return;
        }

        if (character == '\r')
        {
            g_column = 0;
            return;
        }

        if (character == '\b')
        {
            if (g_column > 0)
            {
                --g_column;
            }
            else if (g_row > 0)
            {
                --g_row;
                g_column = VgaWidth - 1;
            }
            else
            {
                return;
            }

            VgaBuffer[g_row * VgaWidth + g_column] = make_entry(' ', g_color);
            return;
        }

        if (character == '\t')
        {
            for (size_t index = 0; index < 4; ++index)
            {
                hardware_put_char(' ');
            }
            return;
        }

        VgaBuffer[g_row * VgaWidth + g_column] = make_entry(character, g_color);
        ++g_column;

        if (g_column >= VgaWidth)
        {
            advance_line();
        }
    }

    void hardware_write(const char* text)
    {
        if (text == nullptr)
        {
            return;
        }

        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            hardware_put_char(text[index]);
        }
    }

    void hardware_write_line(const char* text)
    {
        hardware_write(text);
        hardware_put_char('\n');
    }

    void put_char(char character)
    {
        tinyos::drivers::console::put_char(character);
    }

    void write(const char* text)
    {
        tinyos::drivers::console::write(text);
    }

    void write_line(const char* text)
    {
        tinyos::drivers::console::write_line(text);
    }
}
