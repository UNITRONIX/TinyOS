#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::core::memory
{
    // Fills `count` bytes at `destination` with `value`. A null destination with
    // a non-zero count is a programming error and is rejected rather than
    // faulting, because these helpers run inside kernels and shells where a
    // fault has no reasonable recovery path.
    void* set(void* destination, unsigned char value, size_t count);

    bool buffer_check(size_t buffer_size, size_t required_size);

    // Copies at most `destination_size` bytes. Returns the number of bytes
    // copied. Truncation is silent by design here because the return value
    // tells the caller exactly how much landed; prefer copy_exact() when a
    // partial copy is a bug.
    size_t copy_safe(void* destination, size_t destination_size, const void* source, size_t count);

    // Copies all `count` bytes or nothing. Returns false when the destination is
    // too small or an argument is null, leaving the destination untouched.
    // This exists because the old copy_safe() silently truncated security
    // relevant strings: a caller could not tell a complete copy from a clipped
    // one (defect D12).
    bool copy_exact(void* destination, size_t destination_size, const void* source, size_t count);

    // Terminates the destination string. Returns the number of characters
    // written, excluding the NUL. A null source is treated as an empty string
    // instead of being dereferenced (defect D11).
    size_t string_copy_safe(char* destination, size_t destination_size, const char* source);

    // Like string_copy_safe() but reports whether the whole source fit. Returns
    // false when truncated or when `destination` cannot hold even the NUL.
    bool string_copy_exact(char* destination, size_t destination_size, const char* source);
}
