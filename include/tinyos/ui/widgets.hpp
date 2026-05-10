#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/events.hpp>

namespace tinyos::ui::widgets
{
    enum class Kind : uint32_t
    {
        Label,
        Button
    };

    struct State
    {
        bool ready;
        uint32_t columns;
        uint32_t rows;
        uint32_t content_rows;
    };

    void initialize();
    bool is_ready();
    const State* state();
    bool draw_label(uint32_t row, const char* text);
    bool draw_button(uint32_t row, const char* text, bool focused);
    bool handle_event(const tinyos::ui::events::Event& event);
    size_t dispatch_events(size_t max_events);
    bool render_demo();
    uint64_t label_draw_count();
    uint64_t button_draw_count();
    uint64_t handled_event_count();
    uint64_t activation_count();
    uint64_t rejected_draw_count();
    bool validation_self_test();
    bool event_bridge_validation_self_test();
    const char* kind_name(Kind kind);
}