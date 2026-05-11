#pragma once

#include <stdint.h>

namespace tinyos::ui::graphical_desktop
{
    void initialize();
    bool render_preview();
    bool render();
    bool handle_key(char key);
    bool run_session();
    uint64_t render_count();
    uint64_t handled_key_count();
    uint64_t launch_count();
    bool validation_self_test();
}