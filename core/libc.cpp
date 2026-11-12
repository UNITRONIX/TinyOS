// Freestanding C string/memory primitives.
//
// These are kept in their own translation unit, separate from the
// tinyos::core::memory helpers, for two reasons:
//
//   1. The compiler is free to recognise the loops below and replace them with
//      calls to memcpy()/memset(). When those definitions live in the same
//      translation unit as the loops, that recognition becomes infinite
//      recursion.
//   2. Host unit tests link the tinyos::core::memory helpers but must keep the
//      host C library's memcpy(); defining these symbols in a test binary can
//      silently override libc's version.
//
// The kernel links this file; the host test binaries do not.

#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void* destination, int value, size_t count)
{
    if (destination == nullptr)
    {
        return destination;
    }

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
    if (destination == nullptr || source == nullptr)
    {
        return destination;
    }

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
    if (destination == nullptr || source == nullptr)
    {
        return destination;
    }

    auto* destination_bytes = static_cast<unsigned char*>(destination);
    const auto* source_bytes = static_cast<const unsigned char*>(source);

    // Comparing the pointers directly is undefined behaviour for unrelated
    // objects, and the old code compared them as raw pointers instead of asking
    // whether the ranges actually overlap (defect D20). The integer form below
    // is well defined and answers the only question that matters: is the
    // destination inside the source range?
    const auto destination_address = reinterpret_cast<uintptr_t>(destination_bytes);
    const auto source_address = reinterpret_cast<uintptr_t>(source_bytes);

    const bool destination_inside_source =
        destination_address > source_address &&
        destination_address < source_address + count;

    if (!destination_inside_source)
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

extern "C" int memcmp(const void* left, const void* right, size_t count)
{
    const auto* left_bytes = static_cast<const unsigned char*>(left);
    const auto* right_bytes = static_cast<const unsigned char*>(right);

    // Null is rejected explicitly so a constant-time comparison helper cannot be
    // handed a null buffer and silently "match" (defect D16).
    if (left_bytes == nullptr || right_bytes == nullptr)
    {
        if (left_bytes == right_bytes)
        {
            return 0;
        }

        return left_bytes == nullptr ? -1 : 1;
    }

    for (size_t index = 0; index < count; ++index)
    {
        if (left_bytes[index] != right_bytes[index])
        {
            return static_cast<int>(left_bytes[index]) - static_cast<int>(right_bytes[index]);
        }
    }

    return 0;
}
