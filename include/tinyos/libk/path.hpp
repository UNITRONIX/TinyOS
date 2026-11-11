#pragma once

#include <stddef.h>

namespace tinyos::libk::path
{
    // Canonical path rules for TinyOS VFS paths.
    //
    // These are pure functions with no kernel dependencies so they can be
    // exercised by host unit tests (tests/host/test_path.cpp). The parser used to
    // live inline in kernel/vfs/vfs.cpp, which meant the only way to test a
    // security-relevant validator was to boot the kernel and type at it.
    inline constexpr size_t MaxPathBytes = 128;
    inline constexpr size_t MaxSegmentBytes = 48;

    bool is_allowed_char(char value);

    // A canonical absolute path: starts with '/', no empty segments, no '.',
    // '..', no trailing '/', no control or shell metacharacters, and within the
    // length limits above.
    bool is_valid(const char* path);

    // True when `path` is `prefix` or lives underneath it. Both inputs must be
    // valid canonical paths.
    bool has_prefix(const char* path, const char* prefix);

    // Length of the path without the trailing NUL.
    size_t length(const char* path);
}
