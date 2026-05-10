#pragma once

#include <stddef.h>

namespace tinyos::core::memory
{
    void* set(void* destination, unsigned char value, size_t count);
    bool buffer_check(size_t buffer_size, size_t required_size);
    size_t copy_safe(void* destination, size_t destination_size, const void* source, size_t count);
    size_t string_copy_safe(char* destination, size_t destination_size, const char* source);
}
