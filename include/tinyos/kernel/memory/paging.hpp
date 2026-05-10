#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::paging
{
    void initialize();
    bool is_ready();
    uintptr_t page_directory_address();
    size_t mapped_pages();
    size_t mapped_bytes();
}
