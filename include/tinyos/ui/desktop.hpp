#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::desktop
{
    struct LauncherItem
    {
        const char* title;
        const char* command;
        bool enabled;
    };

    struct State
    {
        bool ready;
        uint32_t columns;
        uint32_t rows;
        size_t item_count;
        size_t selected_index;
    };

    void initialize();
    bool is_ready();
    const State* state();
    size_t launcher_item_count();
    const LauncherItem* launcher_item_at(size_t index);
    const LauncherItem* selected_launcher_item();
    bool select_next();
    bool render_home();
    bool launch_selected();
    bool render_demo();
    uint64_t render_count();
    uint64_t selection_change_count();
    uint64_t launch_request_count();
    uint64_t rejected_operation_count();
    bool validation_self_test();
    bool launcher_validation_self_test();
    bool interaction_validation_self_test();
}