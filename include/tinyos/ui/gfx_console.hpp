#pragma once

#include <stdint.h>

namespace tinyos::ui::gfx_console
{
    void initialize();
    void begin_session();
    void end_session();
    bool session_active();
    void set_serial_mirror(bool enabled);
    bool serial_mirror_enabled();
    bool validation_self_test();
}
