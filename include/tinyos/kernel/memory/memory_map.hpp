#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::map
{
    struct Region
    {
        uint64_t base;
        uint64_t length;
        uint32_t type;
    };

    void initialize(uint32_t multiboot_info_addr);
    size_t region_count();
    const Region& region(size_t index);
    uint64_t total_bytes();
    uint64_t usable_bytes();
}
