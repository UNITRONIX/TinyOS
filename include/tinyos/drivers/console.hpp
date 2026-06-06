#pragma once

#include <stdint.h>

namespace tinyos::drivers::console
{
    enum class Backend : uint8_t
    {
        Vga,
        GfxCapture
    };

    void initialize();
    Backend active_backend();
    void set_backend(Backend backend);
    void set_serial_mirror(bool enabled);
    bool serial_mirror_enabled();
    void put_char(char character);
    void write(const char* text);
    void write_line(const char* text);
    bool validation_self_test();
}
