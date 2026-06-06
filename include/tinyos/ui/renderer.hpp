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
        uint32_t bits_per_pixel;
        uint32_t cell_size;
        bool text_output;
        bool pixel_output;
        bool ready;
    };

    struct Color
    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t alpha;
    };

    void initialize();
    bool initialize_linear_framebuffer();
    bool is_ready();
    const State* state();
    bool draw_text(uint32_t column, uint32_t row, const char* text, uint8_t attribute);
    bool fill_rect(uint32_t column, uint32_t row, uint32_t width, uint32_t height, char fill, uint8_t attribute);
    bool clear_area(uint32_t column, uint32_t row, uint32_t width, uint32_t height, uint8_t attribute);
    uint32_t pack_color(Color color);
    bool draw_pixel(uint32_t x, uint32_t y, Color color);
    bool blend_pixel(uint32_t x, uint32_t y, Color color);
    bool fill_pixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height, Color color);
    bool draw_text_pixels(uint32_t x, uint32_t y, const char* text, Color color);
    uint64_t draw_call_count();
    uint64_t primitive_call_count();
    uint64_t pixel_draw_call_count();
    uint64_t rejected_draw_call_count();
    bool validation_self_test();
    bool primitive_validation_self_test();
    bool pixel_contract_validation_self_test();
    const char* backend_name(Backend backend);
}