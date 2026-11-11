#pragma once

#include <stdint.h>

namespace tinyos::arch::x86_64::interrupts
{
    struct FaultFrame
    {
        uint64_t rip;
        uint64_t cs;
        uint64_t rflags;
        uint64_t rsp;
        uint64_t ss;
    };

    void initialize();
    void enable();
    void disable();
    bool interrupts_enabled();

    // Installs a handler for a hardware vector (32..255). Vectors used by the
    // interrupt controller are routed through the C++ dispatcher, which records
    // counters and then calls the registered callback.
    bool install_handler(uint8_t vector, void (*handler)(uint64_t vector, uint64_t error_code, const FaultFrame* frame));

    uint64_t fault_count();
    uint64_t spurious_count();
    uint64_t last_fault_vector();

    bool validation_self_test();
    const char* exception_name(uint64_t vector);
}
