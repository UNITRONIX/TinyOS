#pragma once

#include <stddef.h>

namespace tinyos::api
{
    void print(const char* text);
    void clear_screen();
    void get_input(char* buffer, size_t max_length);
    void execute_command(const char* input);
}
