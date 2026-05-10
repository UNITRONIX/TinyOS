#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::device
{
    enum class Class : uint32_t
    {
        Console,
        Diagnostics,
        InterruptController,
        Timer,
        Input,
        Filesystem,
        Framebuffer,
        Block,
        Unknown
    };

    enum class State : uint32_t
    {
        Registered,
        Ready,
        Failed
    };

    enum Flags : uint32_t
    {
        FlagNone = 0,
        FlagBootCritical = 1u << 0,
        FlagHardware = 1u << 1,
        FlagVirtual = 1u << 2,
        FlagInterruptDriven = 1u << 3,
        FlagWritable = 1u << 4,
        FlagDiagnostics = 1u << 5,
        FlagReadable = 1u << 6
    };

    struct Device
    {
        const char* name;
        Class device_class;
        State state;
        uint32_t unit;
        uint32_t flags;
    };

    void initialize();
    bool is_ready();
    bool register_device(const char* name, Class device_class, State state, uint32_t unit, uint32_t flags);
    size_t capacity();
    size_t count();
    size_t ready_count();
    size_t class_count(Class device_class);
    size_t rejected_registration_count();
    const Device* at(size_t index);
    const Device* find_by_name(const char* name);
    const Device* find_by_class(Class device_class, size_t ordinal);
    bool has_ready_class(Class device_class);
    bool has_flag(const Device& device, uint32_t flag);
    const char* class_name(Class device_class);
    const char* state_name(State state);
}