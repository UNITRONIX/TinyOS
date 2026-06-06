#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::gfx_terminal
{
    void initialize();
    bool render();
    bool run_session(const char* working_directory);
    uint64_t render_count();
    uint64_t handled_key_count();
    uint64_t command_count();
    bool validation_self_test();
}
