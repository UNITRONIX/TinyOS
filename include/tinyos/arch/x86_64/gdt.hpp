#pragma once

#include <stdint.h>

namespace tinyos::arch::x86_64::gdt
{
    // Selector layout. The order matters: the far jump in the boot stub uses
    // 0x08 and the segment reloads use 0x10.
    inline constexpr uint16_t KernelCodeSelector = 0x08;
    inline constexpr uint16_t KernelDataSelector = 0x10;
    inline constexpr uint16_t UserCodeSelector = 0x18 | 0x3;
    inline constexpr uint16_t UserDataSelector = 0x20 | 0x3;
    inline constexpr uint16_t TssSelector = 0x28;

    // IST slots. Double fault gets its own stack because the whole point of the
    // mechanism is that the faulting stack is unusable.
    inline constexpr uint8_t IstDoubleFault = 1;
    inline constexpr uint8_t IstNmi = 2;
    inline constexpr uint8_t IstMachineCheck = 3;

    void initialize();
    bool is_ready();

    // Points TSS.RSP0 at the stack the CPU must switch to when an interrupt
    // arrives from ring 3. Must be called on every context switch.
    void set_kernel_stack(uint64_t stack_top);

    // Raw IST stack top for a slot, used when building IDT gates.
    uint64_t ist_stack_top(uint8_t index);

    uint16_t kernel_code_selector();
    uint16_t kernel_data_selector();
    uint16_t user_code_selector();
    uint16_t user_data_selector();
    uint16_t tss_selector();

    bool validation_self_test();
}
