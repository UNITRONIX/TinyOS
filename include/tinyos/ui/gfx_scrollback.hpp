#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::gfx_scrollback
{
    constexpr size_t MaxLineLength = 120;
    constexpr size_t MaxLines = 256;

    void initialize();
    void clear();
    void append_char(char character);
    void append_text(const char* text);
    void append_line(const char* text);
    size_t line_count();
    size_t scroll_offset();
    void scroll_up(size_t lines);
    void scroll_down(size_t lines);
    void scroll_to_bottom();
    const char* line_at(size_t index);
    size_t visible_start(size_t viewport_lines);
    size_t visible_count(size_t viewport_lines);
    bool validation_self_test();
}
