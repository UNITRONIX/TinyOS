#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::arch
{
    struct Info
    {
        const char* name;
        const char* cpu_family;
        uint32_t pointer_bits;
        uint32_t page_size;
        bool protected_mode;
        bool paging_supported;
        bool nx_supported;
        bool little_endian;
    };

    void initialize();
    const Info& info();
    bool validation_self_test();
    void load_page_directory(uintptr_t page_directory_address);
    void enable_paging();
    bool paging_enabled();
    uintptr_t active_page_directory();
    [[noreturn]] void halt();
    [[noreturn]] void reboot();

    // Entropy.
    //
    // RDRAND is probed through CPUID at initialise time. When absent,
    // random_bytes() falls back to mixing the timestamp counter with a counter,
    // which is explicitly not cryptographically strong; has_hardware_entropy()
    // lets callers report that difference instead of assuming the best case.
    //
    // This lives under arch because it is a CPU capability, and because salt
    // generation for password hashing must not depend on the scheduler or on any
    // device driver being up.
    bool has_hardware_entropy();
    void random_bytes(void* buffer, size_t length);
    uint64_t timestamp_counter();
}
