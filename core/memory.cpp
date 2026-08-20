#include <stddef.h>
#include <stdint.h>

#include <tinyos/core/memory.hpp>

extern "C" void* memset(void* destination, int value, size_t count)
{
    auto* bytes = static_cast<unsigned char*>(destination);
    const auto fill = static_cast<unsigned char>(value);
    for (size_t index = 0; index < count; ++index)
    {
        bytes[index] = fill;
    }

    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, size_t count)
{
    auto* destination_bytes = static_cast<unsigned char*>(destination);
    const auto* source_bytes = static_cast<const unsigned char*>(source);
    for (size_t index = 0; index < count; ++index)
    {
        destination_bytes[index] = source_bytes[index];
    }

    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t count)
{
    auto* destination_bytes = static_cast<unsigned char*>(destination);
    const auto* source_bytes = static_cast<const unsigned char*>(source);
    if (destination_bytes < source_bytes)
    {
        for (size_t index = 0; index < count; ++index)
        {
            destination_bytes[index] = source_bytes[index];
        }
    }
    else
    {
        for (size_t index = count; index > 0; --index)
        {
            destination_bytes[index - 1] = source_bytes[index - 1];
        }
    }

    return destination;
}

namespace tinyos::core::memory
{
    void* set(void* destination, unsigned char value, size_t count)
    {
        return memset(destination, static_cast<int>(value), count);
    }

    bool buffer_check(size_t buffer_size, size_t required_size)
    {
        return required_size <= buffer_size;
    }

    size_t copy_safe(void* destination, size_t destination_size, const void* source, size_t count)
    {
        if (!buffer_check(destination_size, count))
        {
            count = destination_size;
        }

        memcpy(destination, source, count);
        return count;
    }

    size_t string_copy_safe(char* destination, size_t destination_size, const char* source)
    {
        if (destination_size == 0)
        {
            return 0;
        }

        size_t index = 0;
        while (index + 1 < destination_size && source[index] != '\0')
        {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
        return index;
    }
}
