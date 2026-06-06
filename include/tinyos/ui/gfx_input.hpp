#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::gfx_input
{
    constexpr size_t MaxLineLength = 128;
    constexpr size_t HistorySize = 32;

    struct State
    {
        char buffer[MaxLineLength + 1];
        size_t length;
        size_t cursor;
        size_t history_count;
        size_t history_index;
        char history[HistorySize][MaxLineLength + 1];
    };

    void initialize(State* state);
    void reset(State* state);
    bool handle_key(State* state, char key);
    bool complete_tab(State* state);
    const char* current_line(const State* state);
    bool validation_self_test();
}
