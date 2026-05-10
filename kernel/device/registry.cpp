#include <tinyos/kernel/device/registry.hpp>

namespace
{
    constexpr size_t MaxDevices = 16;

    tinyos::kernel::device::Device g_devices[MaxDevices] = {};
    size_t g_device_count = 0;
    size_t g_rejected_registration_count = 0;
    bool g_ready = false;

    bool same_device(const tinyos::kernel::device::Device& device, tinyos::kernel::device::Class device_class, uint32_t unit)
    {
        return device.device_class == device_class && device.unit == unit;
    }

    bool strings_equal(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return false;
        }

        size_t index = 0;
        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == right[index];
    }
}

namespace tinyos::kernel::device
{
    void initialize()
    {
        g_device_count = 0;
        g_rejected_registration_count = 0;
        g_ready = true;
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool register_device(const char* name, Class device_class, State state, uint32_t unit, uint32_t flags)
    {
        if (!g_ready || name == nullptr || name[0] == '\0' || g_device_count >= MaxDevices)
        {
            ++g_rejected_registration_count;
            return false;
        }

        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (same_device(g_devices[index], device_class, unit))
            {
                ++g_rejected_registration_count;
                return false;
            }
        }

        g_devices[g_device_count].name = name;
        g_devices[g_device_count].device_class = device_class;
        g_devices[g_device_count].state = state;
        g_devices[g_device_count].unit = unit;
        g_devices[g_device_count].flags = flags;
        ++g_device_count;
        return true;
    }

    size_t capacity()
    {
        return MaxDevices;
    }

    size_t count()
    {
        return g_device_count;
    }

    size_t ready_count()
    {
        size_t total = 0;
        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (g_devices[index].state == State::Ready)
            {
                ++total;
            }
        }

        return total;
    }

    size_t class_count(Class device_class)
    {
        size_t total = 0;
        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (g_devices[index].device_class == device_class)
            {
                ++total;
            }
        }

        return total;
    }

    size_t rejected_registration_count()
    {
        return g_rejected_registration_count;
    }

    const Device* at(size_t index)
    {
        if (index >= g_device_count)
        {
            return nullptr;
        }

        return &g_devices[index];
    }

    const Device* find_by_name(const char* name)
    {
        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (strings_equal(g_devices[index].name, name))
            {
                return &g_devices[index];
            }
        }

        return nullptr;
    }

    const Device* find_by_class(Class device_class, size_t ordinal)
    {
        size_t matched = 0;
        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (g_devices[index].device_class != device_class)
            {
                continue;
            }

            if (matched == ordinal)
            {
                return &g_devices[index];
            }

            ++matched;
        }

        return nullptr;
    }

    bool has_ready_class(Class device_class)
    {
        for (size_t index = 0; index < g_device_count; ++index)
        {
            if (g_devices[index].device_class == device_class && g_devices[index].state == State::Ready)
            {
                return true;
            }
        }

        return false;
    }

    bool has_flag(const Device& device, uint32_t flag)
    {
        return (device.flags & flag) != 0;
    }

    const char* class_name(Class device_class)
    {
        switch (device_class)
        {
        case Class::Console:
            return "console";
        case Class::Diagnostics:
            return "diagnostics";
        case Class::InterruptController:
            return "interrupt-controller";
        case Class::Timer:
            return "timer";
        case Class::Input:
            return "input";
        case Class::Filesystem:
            return "filesystem";
        case Class::Framebuffer:
            return "framebuffer";
        case Class::Block:
            return "block";
        case Class::Unknown:
            return "unknown";
        }

        return "unknown";
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Registered:
            return "registered";
        case State::Ready:
            return "ready";
        case State::Failed:
            return "failed";
        }

        return "unknown";
    }
}