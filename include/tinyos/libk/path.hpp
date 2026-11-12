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
    //
    // MaxPathBytes is the single source of truth for path buffers. Before this
    // was centralised the shell used 96, libk used 128 and the VFS used 128, so
    // the shell clipped paths the VFS considered valid (defect D13). Every
    // buffer sized for a path must use this constant.
    inline constexpr size_t MaxPathBytes = 128;
    inline constexpr size_t MaxSegmentBytes = 48;

    // Buffer size needed to hold any valid path plus its terminator.
    inline constexpr size_t MaxPathBufferBytes = MaxPathBytes + 1;

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

    // Resolves `input` against `base` into `output`, honouring "/", ".", "..",
    // and repeated separators. Returns false when the result cannot be
    // represented in `output_size` bytes or does not form a valid path. This is
    // the only implementation of dot-segment resolution in the tree; the shell
    // used to carry a second one with different limits (defect D13).
    bool resolve(const char* base, const char* input, char* output, size_t output_size);

    // Joins `name` onto `parent`. Returns false on overflow or invalid input.
    bool join(const char* parent, const char* name, char* output, size_t output_size);

    // Directory part of a path ("/a/b/c" -> "/a/b"). Root has no parent, so it
    // returns false and leaves `output` empty.
    bool parent_of(const char* path, char* output, size_t output_size);

    // Final component ("/a/b/c" -> "c"). Returns nullptr for the root.
    const char* name_of(const char* path);
}
