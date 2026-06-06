#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/font.hpp>
#include <tinyos/ui/font_atlas.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    bool draw_glyph_rows(uint32_t x, uint32_t y, const uint8_t rows[16], tinyos::ui::renderer::Color ink, tinyos::ui::font_atlas::Style style)
    {
        bool ok = true;
        if (style == tinyos::ui::font_atlas::Style::Normal || style == tinyos::ui::font_atlas::Style::Smooth)
        {
            for (uint32_t row = 0; row < tinyos::ui::font_atlas::GlyphHeight; ++row)
            {
                for (uint32_t column = 0; column < tinyos::ui::font_atlas::GlyphWidth; ++column)
                {
                    if ((rows[row] & (1u << (7 - column))) != 0)
                    {
                        ok = tinyos::ui::renderer::draw_pixel(x + column, y + row, ink) && ok;
                    }
                }
            }

            return ok;
        }

        return false;
    }
}

namespace tinyos::ui::font_atlas
{
    bool glyph_rows(char character, uint8_t rows[GlyphHeight])
    {
        const uint8_t* glyph = tinyos::ui::font::glyph_for(character);
        for (uint32_t row = 0; row < GlyphHeight; ++row)
        {
            const uint32_t source_row = (row * 7u) / GlyphHeight;
            uint8_t value = 0;
            for (uint32_t column = 0; column < GlyphWidth; ++column)
            {
                const uint32_t source_column = (column * 5u) / GlyphWidth;
                if ((glyph[source_column] & (1u << source_row)) != 0)
                {
                    value = static_cast<uint8_t>(value | (1u << (7 - column)));
                }
            }

            rows[row] = value;
        }

        return true;
    }

    bool draw_char(uint32_t x, uint32_t y, char character, renderer::Color ink, Style style)
    {
        uint8_t rows[GlyphHeight];
        if (!glyph_rows(character, rows))
        {
            return false;
        }

        return draw_glyph_rows(x, y, rows, ink, style);
    }

    bool draw_text(uint32_t x, uint32_t y, const char* text, renderer::Color ink, Style style)
    {
        if (text == nullptr)
        {
            return false;
        }

        bool ok = true;
        uint32_t cursor = x;
        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            ok = draw_char(cursor, y, text[index], ink, style) && ok;
            cursor += GlyphAdvance;
        }

        return ok;
    }

    uint32_t text_width(const char* text, Style style)
    {
        (void)style;
        if (text == nullptr)
        {
            return 0;
        }

        size_t length = 0;
        while (text[length] != '\0')
        {
            ++length;
        }

        return static_cast<uint32_t>(length) * GlyphAdvance;
    }

    bool validation_self_test()
    {
        uint8_t rows[GlyphHeight];
        return GlyphWidth == 8 &&
            GlyphHeight == 16 &&
            GlyphAdvance == 9 &&
            glyph_rows('A', rows) &&
            rows[0] != 0;
    }
}
