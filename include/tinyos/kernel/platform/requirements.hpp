#pragma once

#include <stdint.h>

namespace tinyos::kernel::platform::requirements
{
    struct MinimumRequirements
    {
        const char* architecture;
        const char* boot_protocol;
        const char* firmware_path;
        uint32_t minimum_memory_mib;
        uint32_t recommended_memory_mib;
        const char* display;
        const char* input;
        const char* timer;
        const char* interrupt_controller;
        const char* storage;
        const char* emulator;
    };

    const MinimumRequirements& current();
    bool validation_self_test();
}