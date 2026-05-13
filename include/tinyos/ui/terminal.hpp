#pragma once

#include <stdint.h>

#include <tinyos/ui/renderer.hpp>

namespace tinyos::ui::terminal
{
    enum class Style : uint8_t
    {
        Normal,
        Status,
        Accent,
        Success,
        Warning,
        Error,
        Selected,
        Panel,
        Dim
    };

    struct TextSegment
    {
        const char* text;
        Style style;
    };

    struct State
    {
        bool ready;
        tinyos::ui::renderer::Backend backend;
        uint32_t columns;
        uint32_t rows;
        uint32_t status_row;
        uint32_t content_first_row;
        uint32_t content_rows;
    };

    void initialize();
    bool is_ready();
    const State* state();
    bool draw_status(const char* text);
    bool draw_status(const char* text, Style style);
    bool clear_status();
    bool clear_content();
    uint8_t attribute_for_style(Style style);
    bool write_line(uint32_t row, const char* text, uint8_t attribute);
    bool write_line(uint32_t row, const char* text, Style style);
    bool write_segments(uint32_t row, uint32_t column, const TextSegment* segments, uint32_t count);
    bool draw_panel(uint32_t row, uint32_t height, const char* title);
    bool render_self_test_label();
    bool render_panel_self_test();
    bool render_color_demo();
    uint64_t status_update_count();
    uint64_t line_write_count();
    uint64_t clear_operation_count();
    uint64_t panel_draw_count();
    uint64_t rejected_operation_count();
    bool validation_self_test();
    bool panel_validation_self_test();
    bool style_validation_self_test();
}