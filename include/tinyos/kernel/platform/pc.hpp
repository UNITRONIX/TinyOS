#pragma once

#include <stddef.h>

#include <tinyos/kernel/device/registry.hpp>

namespace tinyos::kernel::platform::pc
{
    struct InitPhase
    {
        const char* name;
        const char* responsibility;
        const char* required_device_class;
        bool boot_critical;
    };

    bool validation_self_test();
    size_t phase_count();
    const InitPhase* phase_at(size_t index);
    size_t boot_critical_phase_count();
    size_t required_device_class_count();
    const tinyos::kernel::device::Class* required_device_class_at(size_t index);
    size_t ready_required_device_class_count();
    bool device_contract_satisfied();
}