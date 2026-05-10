#pragma once

namespace tinyos::drivers::serial
{
    void initialize();
    void write_char(char character);
    void write(const char* text);
    void write_line(const char* text);
}
