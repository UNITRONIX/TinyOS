#include <tinyos/drivers/console.hpp>
#include <tinyos/ui/gfx_scrollback.hpp>

namespace
{
    bool g_session_active = false;
    bool g_serial_mirror = false;
}

namespace tinyos::ui::gfx_console
{
    void initialize()
    {
        g_session_active = false;
        g_serial_mirror = false;
    }

    void begin_session()
    {
        g_session_active = true;
        tinyos::ui::gfx_scrollback::clear();
        tinyos::drivers::console::set_backend(tinyos::drivers::console::Backend::GfxCapture);
        tinyos::drivers::console::set_serial_mirror(g_serial_mirror);
    }

    void end_session()
    {
        g_session_active = false;
        tinyos::drivers::console::set_backend(tinyos::drivers::console::Backend::Vga);
        tinyos::drivers::console::set_serial_mirror(false);
    }

    bool session_active()
    {
        return g_session_active;
    }

    void set_serial_mirror(bool enabled)
    {
        g_serial_mirror = enabled;
        if (g_session_active)
        {
            tinyos::drivers::console::set_serial_mirror(enabled);
        }
    }

    bool serial_mirror_enabled()
    {
        return g_serial_mirror;
    }

    bool validation_self_test()
    {
        return !session_active();
    }
}
