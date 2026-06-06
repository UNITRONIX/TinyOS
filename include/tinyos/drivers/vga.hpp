#pragma once

#include <stdint.h>

namespace tinyos::drivers::vga
{
    enum class Color : uint8_t
    {
        Black = 0,
        Blue = 1,
        Green = 2,
        Cyan = 3,
        Red = 4,
        Magenta = 5,
        Brown = 6,
        LightGrey = 7,
        DarkGrey = 8,
        LightBlue = 9,
        LightGreen = 10,
        LightCyan = 11,
        LightRed = 12,
        Pink = 13,
        Yellow = 14,
        White = 15
    };

    void initialize();
    void clear();
    void set_color(Color foreground, Color background);
    void put_char(char character);
    void write(const char* text);
    void write_line(const char* text);
    void hardware_put_char(char character);
    void hardware_write(const char* text);
    void hardware_write_line(const char* text);
}
