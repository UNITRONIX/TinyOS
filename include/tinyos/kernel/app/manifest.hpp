#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::app::manifest
{
    enum class State : uint32_t
    {
        Ready,
        Planned,
        Disabled
    };

    struct AppProfile
    {
        const char* name;
        const char* runtime_name;
        const char* entry_path;
        State state;
        uint32_t requested_capabilities;
        bool gui_app;
        bool self_host_candidate;
        bool trusted_system_app;
    };

    void initialize();
    bool is_ready();
    size_t count();
    size_t ready_count();
    size_t gui_app_count();
    size_t self_host_candidate_count();
    const AppProfile* at(size_t index);
    const AppProfile* find_by_name(const char* name);
    bool capabilities_allowed(const AppProfile& app);
    bool launchable(const AppProfile& app);
    bool validation_self_test();
    const char* state_name(State state);
}