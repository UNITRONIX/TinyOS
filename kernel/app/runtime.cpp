#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    constexpr uint32_t NativeAppCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityFileWrite |
        tinyos::kernel::app::runtime::CapabilityClock;

    constexpr uint32_t GuiAppCapabilities =
        tinyos::kernel::app::runtime::CapabilityConsole |
        tinyos::kernel::app::runtime::CapabilityFileRead |
        tinyos::kernel::app::runtime::CapabilityGui |
        tinyos::kernel::app::runtime::CapabilityClock;

    const tinyos::kernel::app::runtime::Profile g_profiles[] = {
        { "native-c-elf32", "tinyos-native-i686-v0", "ELF32/i386 executable", tinyos::kernel::app::runtime::Language::NativeC, tinyos::kernel::app::runtime::State::Ready, NativeAppCapabilities, false, true },
        { "native-cpp-elf32", "tinyos-native-i686-v0", "ELF32/i386 executable", tinyos::kernel::app::runtime::Language::NativeCpp, tinyos::kernel::app::runtime::State::Ready, NativeAppCapabilities, false, true },
        { "wasm32-sandbox", "tinyos-wasm32-v0", "WASM module", tinyos::kernel::app::runtime::Language::Wasm, tinyos::kernel::app::runtime::State::Planned, GuiAppCapabilities, true, false },
        { "tiny-bytecode", "tinyos-bytecode-v0", "TinyOS bytecode image", tinyos::kernel::app::runtime::Language::Bytecode, tinyos::kernel::app::runtime::State::Planned, GuiAppCapabilities, true, false },
        { "tiny-script", "tinyos-script-v0", "TinyOS script bundle", tinyos::kernel::app::runtime::Language::Script, tinyos::kernel::app::runtime::State::Planned, tinyos::kernel::app::runtime::CapabilityConsole | tinyos::kernel::app::runtime::CapabilityFileRead, true, false }
    };

    bool g_ready = false;
}

namespace tinyos::kernel::app::runtime
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Language runtime manifest initialized.");
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

    size_t planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_profiles[index].state == State::Planned)
            {
                ++planned;
            }
        }

        return planned;
    }

    const Profile* at(size_t index)
    {
        return index < count() ? &g_profiles[index] : nullptr;
    }

    const Profile* active_native_profile()
    {
        for (size_t index = 0; index < count(); ++index)
        {
            const auto& profile = g_profiles[index];
            if (profile.state == State::Ready && (profile.language == Language::NativeC || profile.language == Language::NativeCpp))
            {
                return &profile;
            }
        }

        return nullptr;
    }

    bool supports(Language language)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_profiles[index].language == language && g_profiles[index].state == State::Ready)
            {
                return true;
            }
        }

        return false;
    }

    bool supports_self_hosted_apps()
    {
        const auto* profile = active_native_profile();
        return profile != nullptr && profile->self_host_candidate && capability_allowed(*profile, CapabilityConsole | CapabilityFileRead | CapabilityFileWrite);
    }

    bool capability_allowed(const Profile& profile, uint32_t capabilities)
    {
        return (profile.default_capabilities & capabilities) == capabilities;
    }

    uint32_t default_gui_capabilities()
    {
        return GuiAppCapabilities;
    }

    bool validation_self_test()
    {
        const auto* native = active_native_profile();
        return g_ready &&
            count() == 5 &&
            ready_count() >= 2 &&
            planned_count() >= 3 &&
            supports(Language::NativeC) &&
            supports(Language::NativeCpp) &&
            !supports(Language::Wasm) &&
            native != nullptr &&
            capability_allowed(*native, CapabilityConsole | CapabilityFileRead) &&
            !capability_allowed(*native, CapabilityNetwork) &&
            supports_self_hosted_apps();
    }

    const char* language_name(Language language)
    {
        switch (language)
        {
        case Language::NativeC:
            return "native-c";
        case Language::NativeCpp:
            return "native-cpp";
        case Language::Wasm:
            return "wasm";
        case Language::Bytecode:
            return "bytecode";
        case Language::Script:
            return "script";
        }

        return "unknown";
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

    const char* capability_name(uint32_t capability)
    {
        switch (capability)
        {
        case CapabilityConsole:
            return "console";
        case CapabilityFileRead:
            return "file-read";
        case CapabilityFileWrite:
            return "file-write";
        case CapabilityGui:
            return "gui";
        case CapabilityClock:
            return "clock";
        case CapabilityNetwork:
            return "network";
        }

        return "unknown";
    }
}