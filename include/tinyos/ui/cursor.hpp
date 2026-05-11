#pragma once

#include <stdint.h>

#include <tinyos/ui/events.hpp>

namespace tinyos::ui::cursor
{
    struct State
    {
        bool ready;
        bool visible;
        uint32_t column;
        uint32_t row;
        uint32_t max_columns;
        uint32_t max_rows;
    };

    void initialize();
    bool is_ready();
    const State* state();
    bool set_visible(bool visible);
    bool move_to(uint32_t column, uint32_t row);
    bool move_by(int32_t delta_column, int32_t delta_row);
    bool handle_event(const tinyos::ui::events::Event& event);
    bool render();
    uint64_t movement_count();
    uint64_t render_count();
    uint64_t rejected_operation_count();
    bool validation_self_test();
    bool render_validation_self_test();
}