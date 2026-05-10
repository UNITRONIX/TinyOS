#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::frames
{
    inline constexpr uint32_t FrameSize = 4096;

    void initialize(uint32_t multiboot_info_addr);
    uintptr_t allocate();
    uintptr_t allocate_pages(size_t count);
    void free(uintptr_t address);
    void free_pages(uintptr_t address, size_t count);
    size_t total_frames();
    size_t free_frames();
    size_t reserved_frames();
    size_t allocation_failure_count();
    size_t invalid_free_count();
    size_t double_free_count();
    bool accounting_valid();
}
