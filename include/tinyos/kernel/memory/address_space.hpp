#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::address_space
{
    enum class RegionType
    {
        IdentityMapped,
        KernelImage,
        BootModule
    };

    struct Region
    {
        const char* name;
        uintptr_t virtual_base;
        uintptr_t physical_base;
        size_t size;
        uint32_t flags;
        RegionType type;
    };

    void initialize(uint32_t multiboot_info_addr);
    bool is_ready();
    size_t region_count();
    const Region* region_at(size_t index);
    size_t total_mapped_bytes();
}
