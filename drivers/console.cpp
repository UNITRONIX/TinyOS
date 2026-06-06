#include <tinyos/drivers/console.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/ui/gfx_scrollback.hpp>

namespace
{
    tinyos::drivers::console::Backend g_backend = tinyos::drivers::console::Backend::Vga;
    bool g_serial_mirror = false;

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
        g_serial_mirror = false;
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
        mirror_char(character);
        if (g_backend == Backend::GfxCapture)
        {
            tinyos::ui::gfx_scrollback::append_char(character);
            return;
        }

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

    bool validation_self_test()
    {
        return active_backend() == Backend::Vga;
    }
}
