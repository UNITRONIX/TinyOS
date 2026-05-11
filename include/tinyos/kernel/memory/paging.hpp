#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::paging
{
    inline constexpr uint32_t PageFlagRead = 1u << 0;
    inline constexpr uint32_t PageFlagWrite = 1u << 1;
    inline constexpr uint32_t PageFlagUser = 1u << 2;
    inline constexpr uint32_t PageFlagExecute = 1u << 3;

    struct PageMapping
    {
        uintptr_t virtual_address;
        uintptr_t physical_address;
        uint32_t flags;
        bool present;
    };

    void initialize();
    bool is_ready();
    void enable_runtime();
    bool is_runtime_enabled();
    uintptr_t page_directory_address();
    uintptr_t active_page_directory_address();
    size_t bootstrap_identity_bytes();
    size_t mapped_pages();
    size_t mapped_bytes();
    uint32_t bootstrap_page_flags();
    bool mapping_for(uintptr_t virtual_address, PageMapping& mapping);
    bool update_mapping_flags(uintptr_t virtual_address, uint32_t flags);
    size_t update_mapping_flags_for_range(uintptr_t virtual_base, size_t size, uint32_t flags);
    size_t map_identity_range(uintptr_t physical_base, size_t size, uint32_t flags);
    bool is_bootstrap_identity_mapped(uintptr_t virtual_address);
    bool validation_self_test();
}
