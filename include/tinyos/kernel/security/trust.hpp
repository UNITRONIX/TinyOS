#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::security::trust
{
    enum class KeyUse : uint32_t
    {
        AppPackage,
        ImageManifest,
        Recovery
    };

    enum class State : uint32_t
    {
        Trusted,
        DevelopmentOnly,
        Planned,
        Revoked
    };

    struct TrustAnchor
    {
        const char* name;
        const char* algorithm;
        const char* fingerprint;
        const char* source;
        KeyUse use;
        State state;
        bool permits_app_packages;
        bool permits_images;
    };

    void initialize();
    bool is_ready();
    size_t count();
    size_t trusted_count();
    size_t development_count();
    size_t planned_count();
    size_t app_package_anchor_count();
    size_t image_anchor_count();
    const TrustAnchor* at(size_t index);
    const TrustAnchor* find(const char* name);
    bool algorithm_allowed_for_apps(const char* algorithm);
    bool has_app_package_anchor();
    bool validation_self_test();
    const char* key_use_name(KeyUse use);
    const char* state_name(State state);
}