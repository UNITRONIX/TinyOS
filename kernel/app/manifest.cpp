#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/manifest.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    constexpr uint32_t ConsoleToolCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityFileWrite |
        tinyos::kernel::app::runtime::CapabilityClock;

    constexpr uint32_t GuiShellCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityGui |
        tinyos::kernel::app::runtime::CapabilityClock;

    constexpr uint32_t WebGuiCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityGui |
        tinyos::kernel::app::runtime::CapabilityClock;

    const tinyos::kernel::app::manifest::AppProfile g_profiles[] = {
        { "system-shell", "native-cpp-elf32", "kernel:shell", tinyos::kernel::app::manifest::State::Ready, ConsoleToolCapabilities, false, false, true },
        { "example-system-tool", "native-cpp-elf32", "/apps/example-system-tool.elf", tinyos::kernel::app::manifest::State::Planned, tinyos::kernel::app::runtime::CapabilityConsole | tinyos::kernel::app::runtime::CapabilityFileRead | tinyos::kernel::app::runtime::CapabilityClock, false, false, false },
        { "desktop-shell", "native-cpp-elf32", "/apps/desktop.elf", tinyos::kernel::app::manifest::State::Planned, GuiShellCapabilities, true, false, true },
        { "web-gui-host", "wasm32-sandbox", "/apps/webgui.wasm", tinyos::kernel::app::manifest::State::Planned, WebGuiCapabilities, true, false, false },
        { "selfhost-toolchain", "native-cpp-elf32", "/apps/toolchain.elf", tinyos::kernel::app::manifest::State::Planned, ConsoleToolCapabilities, false, true, true },
        { "bytecode-service", "tiny-bytecode", "/apps/service.tbc", tinyos::kernel::app::manifest::State::Planned, tinyos::kernel::app::runtime::CapabilityConsole | tinyos::kernel::app::runtime::CapabilityFileRead, false, false, false }
    };

    bool g_ready = false;

    bool strings_equal(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return left == right;
        }

        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }

            ++left;
            ++right;
        }

        return *left == '\0' && *right == '\0';
    }

    const tinyos::kernel::app::runtime::Profile* find_runtime(const char* runtime_name)
    {
        for (size_t index = 0; index < tinyos::kernel::app::runtime::count(); ++index)
        {
            const auto* profile = tinyos::kernel::app::runtime::at(index);
            if (profile != nullptr && strings_equal(profile->name, runtime_name))
            {
                return profile;
            }
        }

        return nullptr;
    }
}

namespace tinyos::kernel::app::manifest
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Application capability manifest initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t count()
    {
        return sizeof(g_profiles) / sizeof(g_profiles[0]);
    }

    size_t ready_count()
    {
        size_t ready = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_profiles[index].state == State::Ready)
            {
                ++ready;
            }
        }

        return ready;
    }

    size_t gui_app_count()
    {
        size_t gui_apps = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_profiles[index].gui_app)
            {
                ++gui_apps;
            }
        }

        return gui_apps;
    }

    size_t self_host_candidate_count()
    {
        size_t candidates = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_profiles[index].self_host_candidate)
            {
                ++candidates;
            }
        }

        return candidates;
    }

    const AppProfile* at(size_t index)
    {
        return index < count() ? &g_profiles[index] : nullptr;
    }

    const AppProfile* find_by_name(const char* name)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (strings_equal(g_profiles[index].name, name))
            {
                return &g_profiles[index];
            }
        }

        return nullptr;
    }

    bool capabilities_allowed(const AppProfile& app)
    {
        const auto* runtime_profile = find_runtime(app.runtime_name);
        if (runtime_profile == nullptr)
        {
            return false;
        }

        return runtime::capability_allowed(*runtime_profile, app.requested_capabilities);
    }

    bool launchable(const AppProfile& app)
    {
        const auto* runtime_profile = find_runtime(app.runtime_name);
        return app.state == State::Ready &&
            runtime_profile != nullptr &&
            runtime_profile->state == runtime::State::Ready &&
            capabilities_allowed(app);
    }

    bool validation_self_test()
    {
        const auto* shell = find_by_name("system-shell");
        const auto* web_gui = find_by_name("web-gui-host");
        const auto* selfhost = find_by_name("selfhost-toolchain");

        return g_ready &&
            runtime::is_ready() &&
            count() == 6 &&
            ready_count() == 1 &&
            gui_app_count() >= 2 &&
            self_host_candidate_count() >= 1 &&
            shell != nullptr &&
            launchable(*shell) &&
            web_gui != nullptr &&
            !launchable(*web_gui) &&
            selfhost != nullptr &&
            capabilities_allowed(*selfhost);
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Ready:
            return "ready";
        case State::Planned:
            return "planned";
        case State::Disabled:
            return "disabled";
        }

        return "unknown";
    }
}