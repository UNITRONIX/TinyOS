#pragma once

#include <stdint.h>

#include <tinyos/ui/renderer.hpp>

namespace tinyos::ui::font
{
    constexpr uint32_t GlyphWidth = 5;
    constexpr uint32_t GlyphHeight = 7;
    constexpr uint32_t GlyphAdvance = 6;

    const uint8_t* glyph_for(char character);
    bool draw_char(uint32_t x, uint32_t y, char character, renderer::Color ink, uint32_t pixel_size);
    bool draw_text(uint32_t x, uint32_t y, const char* text, renderer::Color ink, uint32_t pixel_size);
    uint32_t text_width(const char* text, uint32_t pixel_size);
    bool validation_self_test();
}
