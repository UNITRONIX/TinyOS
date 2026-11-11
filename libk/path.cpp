#include <tinyos/libk/path.hpp>

namespace
{
    bool is_dot_segment(const char* path, size_t segment_start, size_t segment_length)
    {
        return (segment_length == 1 && path[segment_start] == '.') ||
            (segment_length == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.');
    }

    bool segment_ok(const char* path, size_t segment_start, size_t segment_length)
    {
        return segment_length != 0 &&
            segment_length <= tinyos::libk::path::MaxSegmentBytes &&
            !is_dot_segment(path, segment_start, segment_length);
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
        if (path == nullptr)
        {
            return 0;
        }

        size_t result = 0;
        while (path[result] != '\0')
        {
            ++result;
        }

        return result;
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
}
