#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/app/manifest.hpp>

namespace tinyos::kernel::app::launcher
{
    enum class Status : uint32_t
    {
        Allowed,
        ManifestNotReady,
        AppNotFound,
        AppNotReady,
        RuntimeNotReady,
        CapabilityDenied
    };

    void initialize();
    bool is_ready();
    Status check(const char* app_name, uint32_t requested_capabilities);
    Status check_profile(const manifest::AppProfile& app, uint32_t requested_capabilities);
    size_t checks_run();
    size_t allowed_count();
    size_t denied_count();
    bool validation_self_test();
    const char* status_name(Status status);
}