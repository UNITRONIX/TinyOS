#include <tinyos/kernel/platform/pc.hpp>

namespace
{
    const tinyos::kernel::platform::pc::InitPhase g_phases[] = {
        { "early-console", "bring up VGA text output for panic-safe diagnostics", "Console", true },
        { "serial-diagnostics", "bring up serial logging before risky subsystems", "Diagnostics", true },
        { "interrupt-controller", "initialize the PIC and keep IRQ rollout explicit", "InterruptController", true },
        { "time-and-input", "initialize PIT, input queue and PS/2 keyboard", "Timer/Input", true },
        { "storage-and-display", "publish RAM block storage and framebuffer surfaces", "Block/Framebuffer", false }
    };

    const tinyos::kernel::device::Class g_required_device_classes[] = {
        tinyos::kernel::device::Class::Console,
        tinyos::kernel::device::Class::Diagnostics,
        tinyos::kernel::device::Class::InterruptController,
        tinyos::kernel::device::Class::Timer,
        tinyos::kernel::device::Class::Input,
        tinyos::kernel::device::Class::Framebuffer,
        tinyos::kernel::device::Class::Block,
        tinyos::kernel::device::Class::Filesystem
    };
}

namespace tinyos::kernel::platform::pc
{
    bool validation_self_test()
    {
        if (phase_count() < 4 || boot_critical_phase_count() < 4 || required_device_class_count() < 8)
        {
            return false;
        }

        for (size_t index = 0; index < phase_count(); ++index)
        {
            const auto* phase = phase_at(index);
            if (phase == nullptr || phase->name == nullptr || phase->responsibility == nullptr || phase->required_device_class == nullptr)
            {
                return false;
            }
        }

        return true;
    }

    size_t phase_count()
    {
        return sizeof(g_phases) / sizeof(g_phases[0]);
    }

    const InitPhase* phase_at(size_t index)
    {
        if (index >= phase_count())
        {
            return nullptr;
        }

        return &g_phases[index];
    }

    size_t boot_critical_phase_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < phase_count(); ++index)
        {
            if (g_phases[index].boot_critical)
            {
                ++count;
            }
        }

        return count;
    }

    size_t required_device_class_count()
    {
        return sizeof(g_required_device_classes) / sizeof(g_required_device_classes[0]);
    }

    const tinyos::kernel::device::Class* required_device_class_at(size_t index)
    {
        if (index >= required_device_class_count())
        {
            return nullptr;
        }

        return &g_required_device_classes[index];
    }

    size_t ready_required_device_class_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < required_device_class_count(); ++index)
        {
            if (tinyos::kernel::device::has_ready_class(g_required_device_classes[index]))
            {
                ++count;
            }
        }

        return count;
    }

    bool device_contract_satisfied()
    {
        return ready_required_device_class_count() == required_device_class_count();
    }
}