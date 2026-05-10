#pragma once

#include <stddef.h>

namespace tinyos::kernel::memory::heap
{
    void initialize();
    void* allocate(size_t size);
    void free(void* pointer);
    size_t total_bytes();
    size_t free_bytes();
    size_t used_bytes();
    size_t block_count();
    size_t allocation_count();
    size_t free_count();
    size_t invalid_free_count();
    size_t double_free_count();
    size_t corrupt_block_count();
    bool state_valid();
}
