#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::app::package
{
    enum class State : uint32_t
    {
        LaunchReady,
        ValidManifest,
        Planned,
        Rejected
    };

    enum class SignatureState : uint32_t
    {
        Missing,
        Present,
        Verified,
        BuiltinTrusted
    };

    enum class PayloadState : uint32_t
    {
        Missing,
        HashListed,
        HashVerified,
        Builtin
    };

    struct Package
    {
        const char* package_name;
        const char* app_name;
        const char* manifest_path;
        const char* runtime_name;
        const char* profile_name;
        const char* entry_path;
        const char* resource_root;
        State state;
        SignatureState signature_state;
        PayloadState payload_state;
        uint32_t required_capabilities;
        bool signed_required;
        bool encryption_supported;
        bool trusted_system_package;
    };

    void initialize();
    bool is_ready();
    size_t count();
    size_t launch_ready_count();
    size_t valid_manifest_count();
    size_t planned_count();
    size_t signed_required_count();
    size_t encryption_supported_count();
    const Package* at(size_t index);
    const Package* find_package(const char* package_name);
    const Package* find_app(const char* app_name);
    bool supports_extension(const char* extension);
    bool profile_matches(const Package& package);
    bool launchable(const Package& package);
    bool validation_self_test();
    const char* extension();
    const char* state_name(State state);
    const char* signature_state_name(SignatureState state);
    const char* payload_state_name(PayloadState state);
}