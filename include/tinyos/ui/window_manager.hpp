#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::window_manager
{
    enum class WindowRole : uint32_t
    {
        Desktop,
        Shell,
        Panel,
        Dialog
    };

    struct Window
    {
        const char* title;
        WindowRole role;
        uint32_t column;
        uint32_t row;
        uint32_t width;
        uint32_t height;
        bool visible;
        bool focused;
    };

    struct State
    {
        bool ready;
        uint32_t columns;
        uint32_t rows;
        size_t window_count;
        size_t focused_index;
    };

    void initialize();
    bool is_ready();
    const State* state();
    size_t window_count();
    const Window* window_at(size_t index);
    const Window* focused_window();
    bool focus_next();
    bool compose();
    bool render_demo();
    uint64_t composition_count();
    uint64_t focus_change_count();
    uint64_t rejected_operation_count();
    bool validation_self_test();
    bool composition_validation_self_test();
    const char* role_name(WindowRole role);
}