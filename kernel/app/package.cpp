#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/manifest.hpp>
#include <tinyos/kernel/app/package.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    using tinyos::kernel::app::package::Package;
    using tinyos::kernel::app::package::PayloadState;
    using tinyos::kernel::app::package::SignatureState;
    using tinyos::kernel::app::package::State;

    constexpr const char* TappExtension = ".tapp";

    constexpr uint32_t ConsoleToolCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityFileWrite |
        tinyos::kernel::app::runtime::CapabilityClock;

    constexpr uint32_t GuiCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityGui |
        tinyos::kernel::app::runtime::CapabilityClock;

    const Package g_packages[] = {
        { "system-shell.tapp", "system-shell", "kernel:system-shell.tapp", "native-cpp-elf32", "system-shell", "kernel:shell", "kernel:", State::LaunchReady, SignatureState::BuiltinTrusted, PayloadState::Builtin, ConsoleToolCapabilities, true, false, true },
        { "example-system-tool.tapp", "example-system-tool", "/apps/example-system-tool.tapp", "native-cpp-elf32", "example-system-tool", "/apps/example-system-tool.elf", "/apps/example-system-tool", State::ValidManifest, SignatureState::Missing, PayloadState::HashListed, tinyos::kernel::app::runtime::CapabilityConsole | tinyos::kernel::app::runtime::CapabilityFileRead | tinyos::kernel::app::runtime::CapabilityClock, true, true, false },
        { "desktop-shell.tapp", "desktop-shell", "/apps/desktop-shell.tapp", "native-cpp-elf32", "desktop-shell", "/apps/desktop.elf", "/apps/desktop", State::Planned, SignatureState::Missing, PayloadState::Missing, GuiCapabilities, true, true, true },
        { "web-gui-host.tapp", "web-gui-host", "/apps/web-gui-host.tapp", "wasm32-sandbox", "web-gui-host", "/apps/webgui.wasm", "/apps/webgui", State::Planned, SignatureState::Missing, PayloadState::Missing, GuiCapabilities, true, true, false },
        { "selfhost-toolchain.tapp", "selfhost-toolchain", "/apps/selfhost-toolchain.tapp", "native-cpp-elf32", "selfhost-toolchain", "/apps/toolchain.elf", "/apps/toolchain", State::Planned, SignatureState::Missing, PayloadState::Missing, ConsoleToolCapabilities, true, true, true }
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
            const auto* runtime_profile = tinyos::kernel::app::runtime::at(index);
            if (runtime_profile != nullptr && strings_equal(runtime_profile->name, runtime_name))
            {
                return runtime_profile;
            }
        }

        return nullptr;
    }
}

namespace tinyos::kernel::app::package
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP package registry initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t count()
    {
        return sizeof(g_packages) / sizeof(g_packages[0]);
    }

    size_t launch_ready_count()
    {
        size_t ready = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_packages[index].state == State::LaunchReady)
            {
                ++ready;
            }
        }

        return ready;
    }

    size_t valid_manifest_count()
    {
        size_t valid = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_packages[index].state == State::LaunchReady || g_packages[index].state == State::ValidManifest)
            {
                ++valid;
            }
        }

        return valid;
    }

    size_t planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_packages[index].state == State::Planned)
            {
                ++planned;
            }
        }

        return planned;
    }

    size_t signed_required_count()
    {
        size_t required = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_packages[index].signed_required)
            {
                ++required;
            }
        }

        return required;
    }

    size_t encryption_supported_count()
    {
        size_t supported = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_packages[index].encryption_supported)
            {
                ++supported;
            }
        }

        return supported;
    }

    const Package* at(size_t index)
    {
        return index < count() ? &g_packages[index] : nullptr;
    }

    const Package* find_package(const char* package_name)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (strings_equal(g_packages[index].package_name, package_name))
            {
                return &g_packages[index];
            }
        }

        return nullptr;
    }

    const Package* find_app(const char* app_name)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (strings_equal(g_packages[index].app_name, app_name))
            {
                return &g_packages[index];
            }
        }

        return nullptr;
    }

    bool supports_extension(const char* extension_name)
    {
        return strings_equal(extension_name, TappExtension);
    }

    bool profile_matches(const Package& package)
    {
        const auto* profile = manifest::find_by_name(package.profile_name);
        if (profile == nullptr)
        {
            return false;
        }

        return strings_equal(profile->runtime_name, package.runtime_name) &&
            strings_equal(profile->entry_path, package.entry_path) &&
            ((profile->requested_capabilities & package.required_capabilities) == package.required_capabilities);
    }

    bool launchable(const Package& package)
    {
        const auto* profile = manifest::find_by_name(package.profile_name);
        const auto* runtime_profile = find_runtime(package.runtime_name);
        return package.state == State::LaunchReady &&
            profile != nullptr &&
            runtime_profile != nullptr &&
            runtime_profile->state == runtime::State::Ready &&
            manifest::launchable(*profile) &&
            profile_matches(package);
    }

    bool validation_self_test()
    {
        const auto* shell = find_package("system-shell.tapp");
        const auto* example = find_app("example-system-tool");
        const auto* desktop = find_package("desktop-shell.tapp");

        return g_ready &&
            runtime::is_ready() &&
            manifest::is_ready() &&
            supports_extension(TappExtension) &&
            count() == 5 &&
            launch_ready_count() == 1 &&
            valid_manifest_count() >= 2 &&
            planned_count() >= 3 &&
            signed_required_count() == count() &&
            encryption_supported_count() >= 4 &&
            shell != nullptr &&
            launchable(*shell) &&
            example != nullptr &&
            profile_matches(*example) &&
            example->signature_state == SignatureState::Missing &&
            example->payload_state == PayloadState::HashListed &&
            !launchable(*example) &&
            desktop != nullptr &&
            !launchable(*desktop);
    }

    const char* extension()
    {
        return TappExtension;
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::LaunchReady:
            return "launch-ready";
        case State::ValidManifest:
            return "valid-manifest";
        case State::Planned:
            return "planned";
        case State::Rejected:
            return "rejected";
        }

        return "unknown";
    }

    const char* signature_state_name(SignatureState state)
    {
        switch (state)
        {
        case SignatureState::Missing:
            return "missing";
        case SignatureState::Present:
            return "present";
        case SignatureState::Verified:
            return "verified";
        case SignatureState::BuiltinTrusted:
            return "builtin-trusted";
        }

        return "unknown";
    }

    const char* payload_state_name(PayloadState state)
    {
        switch (state)
        {
        case PayloadState::Missing:
            return "missing";
        case PayloadState::HashListed:
            return "hash-listed";
        case PayloadState::HashVerified:
            return "hash-verified";
        case PayloadState::Builtin:
            return "builtin";
        }

        return "unknown";
    }
}