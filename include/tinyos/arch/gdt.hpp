#pragma once

#include <stdint.h>

namespace tinyos::arch::gdt
{
    void initialize();
    bool is_ready();
    uint16_t kernel_code_selector();
    uint16_t kernel_data_selector();
    uint16_t user_code_selector();
    uint16_t user_data_selector();
    uint16_t tss_selector();
    void set_kernel_stack(uint32_t stack_top);
    bool validation_self_test();
}

extern "C" void enter_user_mode(uint32_t entry, uint32_t user_stack_top, uint16_t user_data_sel, uint16_t user_code_sel);
extern "C" uint32_t launch_user_and_wait(uint32_t entry, uint32_t user_stack_top, uint16_t user_data_sel, uint16_t user_code_sel);
extern "C" void return_from_user_exit(uint32_t code);
