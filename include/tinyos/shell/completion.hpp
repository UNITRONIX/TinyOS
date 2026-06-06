#pragma once

#include <stddef.h>

namespace tinyos::shell::completion
{
    bool complete_prefix(const char* prefix, char* output, size_t output_size);
    size_t command_count();
    const char* command_at(size_t index);
    bool validation_self_test();
}
