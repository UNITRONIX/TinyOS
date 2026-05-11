#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/events.hpp>

namespace tinyos::ui::desktop
{
    struct LauncherItem
    {
        const char* title;
        const char* command;
        bool enabled;
    };

    struct DesktopIcon
    {
        const char* title;
        const char* command;
        uint32_t column;
        uint32_t row;
        bool selected;
    };

    struct AppWindow
    {
        const char* title;
        const char* command;
        uint32_t column;
        uint32_t row;
        uint32_t width;
        uint32_t height;
        bool open;
        bool focused;
    };

    struct State
    {
        bool ready;
        uint32_t columns;
        uint32_t rows;
        size_t item_count;
        size_t selected_index;
        size_t icon_count;
        size_t app_window_count;
        uint32_t pointer_column;
        uint32_t pointer_row;
        bool fullscreen;
    };

    void initialize();
    bool is_ready();
    const State* state();
    size_t launcher_item_count();
    const LauncherItem* launcher_item_at(size_t index);
    const LauncherItem* selected_launcher_item();
    size_t icon_count();
    const DesktopIcon* icon_at(size_t index);
    size_t app_window_count();
    const AppWindow* app_window_at(size_t index);
    size_t open_app_window_count();
    bool select_next();
    bool select_previous();
    bool render_home();
    bool render_fullscreen();
    bool launch_selected();
    bool render_demo();
    bool handle_event(const tinyos::ui::events::Event& event);
    size_t dispatch_events(size_t max_events);
    uint64_t render_count();
    uint64_t selection_change_count();
    uint64_t launch_request_count();
    uint64_t handled_event_count();
    uint64_t pointer_event_count();
    uint64_t rejected_operation_count();
    bool validation_self_test();
    bool launcher_validation_self_test();
    bool interaction_validation_self_test();
    bool fullscreen_validation_self_test();
    bool input_validation_self_test();
}