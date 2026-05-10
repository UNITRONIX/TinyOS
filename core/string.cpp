#include <tinyos/core/string.hpp>

namespace tinyos::core::string
{
    size_t length(const char* text)
    {
        size_t result = 0;

        while (text[result] != '\0')
        {
            ++result;
        }

        return result;
    }

    int compare(const char* left, const char* right)
    {
        while (*left != '\0' && *left == *right)
        {
            ++left;
            ++right;
        }

        return static_cast<unsigned char>(*left) - static_cast<unsigned char>(*right);
    }

    bool starts_with(const char* text, const char* prefix)
    {
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

    const char* skip_spaces(const char* text)
    {
        while (*text == ' ')
        {
            ++text;
        }

        return text;
    }
}
