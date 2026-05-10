#pragma once

#include <stdint.h>

namespace tinyos::ui::renderer
{
    enum class Backend : uint32_t
    {
        None,
        TextGrid,
        LinearFramebuffer
    };

    struct State
    {
        const char* surface_name;
        Backend backend;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint32_t cell_size;
        bool text_output;
        bool pixel_output;
        bool ready;
    };

    void initialize();
    bool is_ready();
    const State* state();
    bool draw_text(uint32_t column, uint32_t row, const char* text, uint8_t attribute);
    bool fill_rect(uint32_t column, uint32_t row, uint32_t width, uint32_t height, char fill, uint8_t attribute);
    bool clear_area(uint32_t column, uint32_t row, uint32_t width, uint32_t height, uint8_t attribute);
    uint64_t draw_call_count();
    uint64_t primitive_call_count();
    uint64_t rejected_draw_call_count();
    bool validation_self_test();
    bool primitive_validation_self_test();
    const char* backend_name(Backend backend);
}