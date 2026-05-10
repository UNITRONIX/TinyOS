#pragma once

#include <stddef.h>

namespace tinyos::kernel::security::integrity
{
    void initialize();
    bool is_ready();
    bool allocator_state_valid();
    bool boot_modules_valid();
    size_t checks_run();
}
