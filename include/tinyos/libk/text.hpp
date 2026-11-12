#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::libk::text
{
    // Pure, kernel-free helpers. They live in libk so the host test binary and
    // the kernel link the same implementation instead of each hand-rolling
    // argument parsing (defect D24).

    size_t length(const char* text);
    int compare(const char* left, const char* right);
    int compare_n(const char* left, const char* right, size_t count);
    bool equals(const char* left, const char* right);
    bool starts_with(const char* text, const char* prefix);
    bool ends_with(const char* text, const char* suffix);
    const char* find(const char* haystack, const char* needle);
    const char* skip_spaces(const char* text);
    size_t trim_right(char* text);

    // Compares two byte sequences without an early exit. Used for password and
    // hash comparison, where a timing-dependent memcmp() leaks how many leading
    // bytes were correct.
    bool equals_constant_time(const void* left, const void* right, size_t count);

    // Parses a signed 64-bit integer. Returns false on overflow, on an empty
    // input, or when trailing characters remain (`strict` controls the latter).
    bool parse_int64(const char* text, int64_t& value, bool strict);
    bool parse_uint64(const char* text, uint64_t& value, bool strict);

    // Parses an octal mode such as "0644" or "755". Rejects any digit outside
    // 0..7 and any value that does not fit in 16 bits.
    bool parse_octal(const char* text, uint16_t& value);

    // Splits `text` on `separator` into at most `max_parts` pieces. Segments are
    // not copied: `begins` and `lengths` describe slices of the input.
    size_t split(const char* text, char separator, const char* begins[], size_t lengths[], size_t max_parts);
}
