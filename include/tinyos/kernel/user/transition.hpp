#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace tinyos::kernel::user::transition
{
    void initialize();
    bool is_ready();
    uint32_t syscall_vector();
    uint16_t user_code_selector();
    uint16_t user_data_selector();
    uint32_t user_stack_alignment();
}
