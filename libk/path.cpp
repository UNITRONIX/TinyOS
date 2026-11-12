#include <tinyos/libk/path.hpp>

#include <tinyos/core/memory.hpp>
#include <tinyos/libk/text.hpp>

namespace
{
    using tinyos::libk::path::MaxPathBytes;
    using tinyos::libk::path::MaxSegmentBytes;

    bool is_dot_segment(const char* path, size_t segment_start, size_t segment_length)
    {
        return (segment_length == 1 && path[segment_start] == '.') ||
            (segment_length == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.');
    }

    bool segment_ok(const char* path, size_t segment_start, size_t segment_length)
    {
        return segment_length != 0 &&
            segment_length <= MaxSegmentBytes &&
            !is_dot_segment(path, segment_start, segment_length);
    }

    // Removes the final component, never walking above the root.
    bool pop_segment(char* path, size_t path_size)
    {
        (void)path_size;

        if (path == nullptr || path[0] != '/')
        {
            return false;
        }

        if (path[1] == '\0')
        {
            return true;
        }

        size_t path_length = tinyos::libk::text::length(path);
        while (path_length > 1 && path[path_length - 1] == '/')
        {
            path[path_length - 1] = '\0';
            --path_length;
        }

        size_t slash = path_length - 1;
        while (slash > 0 && path[slash] != '/')
        {
            --slash;
        }

        // "/a" becomes "/" rather than an empty string, so ".." at the top level
        // is a no-op instead of an escape (defect D23 class: unbounded walking).
        path[slash == 0 ? 1 : slash] = '\0';
        return true;
    }

    bool append_segment(char* path, size_t path_size, const char* segment, size_t segment_length)
    {
        if (path == nullptr || path_size == 0 || segment == nullptr)
        {
            return false;
        }

        if (segment_length == 0)
        {
            return true;
        }

        if (is_dot_segment(segment, 0, segment_length))
        {
            if (segment_length == 1)
            {
                return true;
            }

            return pop_segment(path, path_size);
        }

        if (segment_length > MaxSegmentBytes)
        {
            return false;
        }

        const size_t path_length = tinyos::libk::text::length(path);
        if (path_length == 0 || path[0] != '/')
        {
            return false;
        }

        const bool need_separator = !(path_length == 1 && path[0] == '/');
        if (path_length + (need_separator ? 1u : 0u) + segment_length + 1u > path_size)
        {
            return false;
        }

        size_t write_index = path_length;
        if (need_separator)
        {
            path[write_index] = '/';
            ++write_index;
        }

        for (size_t index = 0; index < segment_length; ++index)
        {
            if (!tinyos::libk::path::is_allowed_char(segment[index]))
            {
                return false;
            }

            path[write_index + index] = segment[index];
        }

        path[write_index + segment_length] = '\0';
        return true;
    }
}

namespace tinyos::libk::path
{
    bool is_allowed_char(char value)
    {
        return (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '-' ||
            value == '_' ||
            value == '.';
    }

    size_t length(const char* path)
    {
        return tinyos::libk::text::length(path);
    }

    bool is_valid(const char* path)
    {
        if (path == nullptr || path[0] != '/')
        {
            return false;
        }

        if (path[1] == '\0')
        {
            return true;
        }

        size_t index = 1;
        size_t segment_start = 1;
        size_t segment_length = 0;

        while (path[index] != '\0')
        {
            if (index >= MaxPathBytes)
            {
                return false;
            }

            if (path[index] == '/')
            {
                if (!segment_ok(path, segment_start, segment_length))
                {
                    return false;
                }

                segment_start = index + 1;
                segment_length = 0;
                ++index;
                continue;
            }

            if (!is_allowed_char(path[index]))
            {
                return false;
            }

            ++segment_length;
            ++index;
        }

        return segment_ok(path, segment_start, segment_length);
    }

    bool has_prefix(const char* path, const char* prefix)
    {
        if (path == nullptr || prefix == nullptr)
        {
            return false;
        }

        const size_t prefix_length = length(prefix);
        if (prefix_length == 0)
        {
            return false;
        }

        for (size_t index = 0; index < prefix_length; ++index)
        {
            if (path[index] != prefix[index])
            {
                return false;
            }
        }

        return path[prefix_length] == '\0' || path[prefix_length] == '/';
    }

    bool resolve(const char* base, const char* input, char* output, size_t output_size)
    {
        if (output == nullptr || output_size == 0 || input == nullptr)
        {
            return false;
        }

        input = tinyos::libk::text::skip_spaces(input);

        // Absolute input ignores the base entirely; relative input starts from
        // it. A null base is treated as the root rather than rejected, because
        // callers legitimately resolve against "nowhere" during early boot.
        if (input[0] == '/')
        {
            if (!tinyos::core::memory::string_copy_exact(output, output_size, "/"))
            {
                return false;
            }
        }
        else
        {
            const char* effective_base = (base == nullptr || base[0] != '/') ? "/" : base;
            if (!tinyos::core::memory::string_copy_exact(output, output_size, effective_base))
            {
                return false;
            }
        }

        const char* cursor = input;
        while (*cursor == '/')
        {
            ++cursor;
        }

        while (*cursor != '\0')
        {
            const char* segment = cursor;
            size_t segment_length = 0;
            while (cursor[segment_length] != '\0' && cursor[segment_length] != '/')
            {
                ++segment_length;
            }

            if (!append_segment(output, output_size, segment, segment_length))
            {
                return false;
            }

            cursor += segment_length;
            while (*cursor == '/')
            {
                ++cursor;
            }
        }

        return is_valid(output);
    }

    bool join(const char* parent, const char* name, char* output, size_t output_size)
    {
        return resolve(parent, name, output, output_size);
    }

    bool parent_of(const char* path, char* output, size_t output_size)
    {
        if (path == nullptr || output == nullptr || output_size == 0)
        {
            return false;
        }

        if (!tinyos::core::memory::string_copy_exact(output, output_size, path))
        {
            return false;
        }

        if (output[1] == '\0')
        {
            // The root has no parent.
            output[0] = '\0';
            return false;
        }

        return pop_segment(output, output_size);
    }

    const char* name_of(const char* path)
    {
        if (path == nullptr)
        {
            return nullptr;
        }

        const size_t path_length = length(path);
        if (path_length <= 1)
        {
            return nullptr;
        }

        size_t slash = path_length;
        while (slash > 0 && path[slash - 1] != '/')
        {
            --slash;
        }

        return path + slash;
    }
}
