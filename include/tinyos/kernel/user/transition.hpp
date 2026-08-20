#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::user::transition
{
    void initialize();
    bool is_ready();
    uint32_t syscall_vector();
    uint16_t user_code_selector();
    uint16_t user_data_selector();
    uint32_t user_stack_alignment();
    const char* init_process_name();
    const char* init_entry_path();
    uintptr_t init_user_stack_top();
    size_t init_user_stack_bytes();
    bool init_launch_supported();
    bool initial_process_contract_ready();
    bool validation_self_test();
    bool launch_init();
    void note_init_exit(uint32_t code);
    bool init_exited();
    uint32_t init_exit_code();
}
