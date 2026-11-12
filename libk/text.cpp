#include <tinyos/libk/text.hpp>

namespace
{
    bool is_digit(char value)
    {
        return value >= '0' && value <= '9';
    }

    bool is_space(char value)
    {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n';
    }

    // Accumulates one digit into `value`, reporting overflow instead of silently
    // wrapping. Wrapping was the reason the old shell accepted absurd numbers.
    bool accumulate(uint64_t& value, uint64_t digit, uint64_t limit)
    {
        if (value > (limit - digit) / 10)
        {
            return false;
        }

        value = value * 10 + digit;
        return true;
    }
}

namespace tinyos::libk::text
{
    size_t length(const char* text)
    {
        if (text == nullptr)
        {
            return 0;
        }

        size_t result = 0;
        while (text[result] != '\0')
        {
            ++result;
        }

        return result;
    }

    int compare(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            if (left == right)
            {
                return 0;
            }

            return left == nullptr ? -1 : 1;
        }

        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }

        return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
    }

    int compare_n(const char* left, const char* right, size_t count)
    {
        if (left == nullptr || right == nullptr)
        {
            if (left == right)
            {
                return 0;
            }

            return left == nullptr ? -1 : 1;
        }

        for (size_t index = 0; index < count; ++index)
        {
            const auto left_byte = static_cast<unsigned char>(left[index]);
            const auto right_byte = static_cast<unsigned char>(right[index]);
            if (left_byte != right_byte)
            {
                return static_cast<int>(left_byte) - static_cast<int>(right_byte);
            }

            if (left_byte == 0)
            {
                return 0;
            }
        }

        return 0;
    }

    bool equals(const char* left, const char* right)
    {
        return compare(left, right) == 0;
    }

    bool starts_with(const char* text, const char* prefix)
    {
        if (text == nullptr || prefix == nullptr)
        {
            return false;
        }

        while (*prefix != '\0')
        {
            if (*text != *prefix)
            {
                return false;
            }

            ++text;
            ++prefix;
        }

        return true;
    }

    bool ends_with(const char* text, const char* suffix)
    {
        if (text == nullptr || suffix == nullptr)
        {
            return false;
        }

        const size_t text_length = length(text);
        const size_t suffix_length = length(suffix);
        if (suffix_length > text_length)
        {
            return false;
        }

        return compare_n(text + (text_length - suffix_length), suffix, suffix_length) == 0;
    }

    const char* find(const char* haystack, const char* needle)
    {
        if (haystack == nullptr || needle == nullptr || needle[0] == '\0')
        {
            return haystack;
        }

        for (const char* candidate = haystack; *candidate != '\0'; ++candidate)
        {
            if (compare_n(candidate, needle, length(needle)) == 0)
            {
                return candidate;
            }
        }

        return nullptr;
    }

    const char* skip_spaces(const char* text)
    {
        if (text == nullptr)
        {
            return text;
        }

        while (is_space(*text))
        {
            ++text;
        }

        return text;
    }

    size_t trim_right(char* text)
    {
        if (text == nullptr)
        {
            return 0;
        }

        size_t text_length = length(text);
        while (text_length > 0 && is_space(text[text_length - 1]))
        {
            --text_length;
            text[text_length] = '\0';
        }

        return text_length;
    }

    bool equals_constant_time(const void* left, const void* right, size_t count)
    {
        const auto* left_bytes = static_cast<const unsigned char*>(left);
        const auto* right_bytes = static_cast<const unsigned char*>(right);

        if (left_bytes == nullptr || right_bytes == nullptr)
        {
            return false;
        }

        // The accumulator keeps every byte comparison reachable: no branch
        // depends on the data, so the run time does not reveal the first
        // mismatching byte.
        unsigned char difference = 0;
        for (size_t index = 0; index < count; ++index)
        {
            difference |= static_cast<unsigned char>(left_bytes[index] ^ right_bytes[index]);
        }

        return difference == 0;
    }

    bool parse_uint64(const char* text, uint64_t& value, bool strict)
    {
        value = 0;
        if (text == nullptr)
        {
            return false;
        }

        text = skip_spaces(text);
        if (*text == '+')
        {
            ++text;
        }

        if (!is_digit(*text))
        {
            return false;
        }

        uint64_t result = 0;
        while (is_digit(*text))
        {
            if (!accumulate(result, static_cast<uint64_t>(*text - '0'), UINT64_MAX))
            {
                return false;
            }

            ++text;
        }

        if (strict)
        {
            text = skip_spaces(text);
            if (*text != '\0')
            {
                return false;
            }
        }

        value = result;
        return true;
    }

    bool parse_int64(const char* text, int64_t& value, bool strict)
    {
        value = 0;
        if (text == nullptr)
        {
            return false;
        }

        text = skip_spaces(text);
        bool negative = false;
        if (*text == '-' || *text == '+')
        {
            negative = *text == '-';
            ++text;
        }

        if (!is_digit(*text))
        {
            return false;
        }

        // The negative range holds one more value than the positive one, so the
        // magnitude limit differs per sign.
        const uint64_t limit = negative
            ? static_cast<uint64_t>(INT64_MAX) + 1ull
            : static_cast<uint64_t>(INT64_MAX);

        uint64_t magnitude = 0;
        while (is_digit(*text))
        {
            if (!accumulate(magnitude, static_cast<uint64_t>(*text - '0'), limit))
            {
                return false;
            }

            ++text;
        }

        if (strict)
        {
            text = skip_spaces(text);
            if (*text != '\0')
            {
                return false;
            }
        }

        if (negative)
        {
            value = magnitude == static_cast<uint64_t>(INT64_MAX) + 1ull
                ? INT64_MIN
                : -static_cast<int64_t>(magnitude);
        }
        else
        {
            value = static_cast<int64_t>(magnitude);
        }

        return true;
    }

    bool parse_octal(const char* text, uint16_t& value)
    {
        value = 0;
        if (text == nullptr)
        {
            return false;
        }

        text = skip_spaces(text);
        if (!(*text >= '0' && *text <= '7'))
        {
            return false;
        }

        uint32_t result = 0;
        while (*text >= '0' && *text <= '7')
        {
            result = result * 8u + static_cast<uint32_t>(*text - '0');
            if (result > 0xFFFFu)
            {
                return false;
            }

            ++text;
        }

        text = skip_spaces(text);
        if (*text != '\0')
        {
            return false;
        }

        value = static_cast<uint16_t>(result);
        return true;
    }

    size_t split(const char* text, char separator, const char* begins[], size_t lengths[], size_t max_parts)
    {
        if (text == nullptr || begins == nullptr || lengths == nullptr || max_parts == 0)
        {
            return 0;
        }

        size_t part_count = 0;
        const char* segment_begin = text;

        for (const char* cursor = text;; ++cursor)
        {
            const bool at_separator = *cursor == separator;
            const bool at_end = *cursor == '\0';

            if (at_separator || at_end)
            {
                if (part_count < max_parts)
                {
                    begins[part_count] = segment_begin;
                    lengths[part_count] = static_cast<size_t>(cursor - segment_begin);
                    ++part_count;
                }

                segment_begin = cursor + 1;
            }

            if (at_end)
            {
                break;
            }
        }

        return part_count;
    }
}
