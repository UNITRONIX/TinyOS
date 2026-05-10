#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::app::runtime
{
    enum class Language : uint32_t
    {
        NativeC,
        NativeCpp,
        Wasm,
        Bytecode,
        Script
    };

    enum class State : uint32_t
    {
        Ready,
        Planned,
        Disabled
    };

    enum Capability : uint32_t
    {
        CapabilityNone = 0,
        CapabilityConsole = 1u << 0,
        CapabilityFileRead = 1u << 1,
        CapabilityFileWrite = 1u << 2,
        CapabilityGui = 1u << 3,
        CapabilityClock = 1u << 4,
        CapabilityNetwork = 1u << 5
    };

    struct Profile
    {
        const char* name;
        const char* abi;
        const char* entry_format;
        Language language;
        State state;
        uint32_t default_capabilities;
        bool sandboxed;
        bool self_host_candidate;
    };

    void initialize();
    bool is_ready();
    size_t count();
    size_t ready_count();
    size_t planned_count();
    const Profile* at(size_t index);
    const Profile* active_native_profile();
    bool supports(Language language);
    bool supports_self_hosted_apps();
    bool capability_allowed(const Profile& profile, uint32_t capabilities);
    uint32_t default_gui_capabilities();
    bool validation_self_test();
    const char* language_name(Language language);
    const char* state_name(State state);
    const char* capability_name(uint32_t capability);
}