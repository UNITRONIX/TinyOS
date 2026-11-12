// Safe copy helpers layered over the freestanding mem* primitives.
//
// These live apart from core/libc.cpp so host unit tests can link this file
// while keeping the host C library's memcpy(). See core/libc.cpp for why the
// split exists.

#include <stddef.h>

#include <tinyos/core/memory.hpp>

extern "C" void* memcpy(void* destination, const void* source, size_t count);

namespace tinyos::core::memory
{
    void* set(void* destination, unsigned char value, size_t count)
    {
        if (destination == nullptr)
        {
            return destination;
        }

        auto* bytes = static_cast<unsigned char*>(destination);
        for (size_t index = 0; index < count; ++index)
        {
            bytes[index] = value;
        }

        return destination;
    }

    bool buffer_check(size_t buffer_size, size_t required_size)
    {
        return required_size <= buffer_size;
    }

    size_t copy_safe(void* destination, size_t destination_size, const void* source, size_t count)
    {
        if (destination == nullptr || source == nullptr || destination_size == 0)
        {
            return 0;
        }

        if (!buffer_check(destination_size, count))
        {
            count = destination_size;
        }

        memcpy(destination, source, count);
        return count;
    }

    bool copy_exact(void* destination, size_t destination_size, const void* source, size_t count)
    {
        if (destination == nullptr || source == nullptr)
        {
            return false;
        }

        if (!buffer_check(destination_size, count))
        {
            return false;
        }

        if (count != 0)
        {
            memcpy(destination, source, count);
        }

        return true;
    }

    size_t string_copy_safe(char* destination, size_t destination_size, const char* source)
    {
        if (destination == nullptr || destination_size == 0)
        {
            return 0;
        }

        // A null source used to be dereferenced here (defect D11). Treating it
        // as an empty string keeps every existing caller working while removing
        // the fault.
        if (source == nullptr)
        {
            destination[0] = '\0';
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

    bool string_copy_exact(char* destination, size_t destination_size, const char* source)
    {
        if (destination == nullptr || destination_size == 0)
        {
            return false;
        }

        if (source == nullptr)
        {
            destination[0] = '\0';
            return true;
        }

        size_t index = 0;
        while (source[index] != '\0')
        {
            if (index + 1 >= destination_size)
            {
                destination[0] = '\0';
                return false;
            }

            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
        return true;
    }
}
