#pragma once

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
}
