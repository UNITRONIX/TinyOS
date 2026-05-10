#pragma once

#include <stddef.h>

namespace tinyos::core::string
{
    size_t length(const char* text);
    int compare(const char* left, const char* right);
    bool starts_with(const char* text, const char* prefix);
    const char* skip_spaces(const char* text);
}
