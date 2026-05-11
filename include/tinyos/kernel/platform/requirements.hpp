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

    struct PlatformProfile
    {
        const char* name;
        const char* machine_class;
        const char* boot_media;
        const char* console_device;
        const char* input_device;
        const char* timer_device;
        const char* interrupt_controller;
        const char* storage_model;
        bool static_driver_model;
        bool emulator_first;
    };

    const MinimumRequirements& current();
    const PlatformProfile& platform();
    bool validation_self_test();
    bool platform_validation_self_test();
}