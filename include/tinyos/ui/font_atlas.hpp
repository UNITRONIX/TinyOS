#pragma once

#include <stdint.h>

#include <tinyos/ui/renderer.hpp>

namespace tinyos::ui::font_atlas
{
    constexpr uint32_t GlyphWidth = 8;
    constexpr uint32_t GlyphHeight = 16;
    constexpr uint32_t GlyphAdvance = 9;

    enum class Style : uint8_t
    {
        Normal,
        Smooth
    };

    bool glyph_rows(char character, uint8_t rows[GlyphHeight]);
    bool draw_char(uint32_t x, uint32_t y, char character, renderer::Color ink, Style style);
    bool draw_text(uint32_t x, uint32_t y, const char* text, renderer::Color ink, Style style);
    uint32_t text_width(const char* text, Style style);
    bool validation_self_test();
}
