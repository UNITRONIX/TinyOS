#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/font_logo.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    const uint8_t* block_glyph_for(char character)
    {
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 32);
        }

        static constexpr uint8_t A[10] = { 0xFF, 0xFF, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0xFF, 0xFF };
        static constexpr uint8_t B[10] = { 0xFE, 0xFE, 0x92, 0x92, 0xFE, 0xFE, 0x92, 0x92, 0xFE, 0xFE };
        static constexpr uint8_t C[10] = { 0x7E, 0x7E, 0x81, 0x81, 0x80, 0x80, 0x81, 0x81, 0x7E, 0x7E };
        static constexpr uint8_t D[10] = { 0xFC, 0xFC, 0x82, 0x82, 0x81, 0x81, 0x82, 0x82, 0xFC, 0xFC };
        static constexpr uint8_t E[10] = { 0xFF, 0xFF, 0x80, 0x80, 0xF8, 0xF8, 0x80, 0x80, 0xFF, 0xFF };
        static constexpr uint8_t F[10] = { 0xFF, 0xFF, 0x80, 0x80, 0xF8, 0xF8, 0x80, 0x80, 0x80, 0x80 };
        static constexpr uint8_t G[10] = { 0x7E, 0x7E, 0x81, 0x81, 0x8F, 0x8F, 0x81, 0x81, 0x7E, 0x7E };
        static constexpr uint8_t H[10] = { 0xFF, 0xFF, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xFF, 0xFF };
        static constexpr uint8_t I[10] = { 0x7E, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x7E };
        static constexpr uint8_t N[10] = { 0xFF, 0xFF, 0x08, 0x08, 0x14, 0x14, 0x22, 0x22, 0xFF, 0xFF };
        static constexpr uint8_t O[10] = { 0x7E, 0x7E, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x7E, 0x7E };
        static constexpr uint8_t S[10] = { 0x7E, 0x7E, 0x80, 0x80, 0x7E, 0x7E, 0x01, 0x01, 0xFE, 0xFE };
        static constexpr uint8_t T[10] = { 0xFF, 0xFF, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18 };
        static constexpr uint8_t Y[10] = { 0xFF, 0xFF, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x70, 0x70 };
        static constexpr uint8_t Space[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

        switch (character)
        {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D; case 'E': return E;
        case 'F': return F; case 'G': return G; case 'H': return H; case 'I': return I; case 'N': return N;
        case 'O': return O; case 'S': return S; case 'T': return T; case 'Y': return Y; case ' ': return Space;
        default: return Space;
        }
    }

    bool draw_block_char(uint32_t x, uint32_t y, char character, tinyos::ui::renderer::Color ink, uint32_t pixel_size)
    {
        const uint8_t* glyph = block_glyph_for(character);
        bool ok = true;
        for (uint32_t column = 0; column < tinyos::ui::font_logo::GlyphWidth; ++column)
        {
            for (uint32_t row = 0; row < tinyos::ui::font_logo::GlyphHeight; ++row)
            {
                if ((glyph[column] & (1u << row)) != 0)
                {
                    ok = tinyos::ui::renderer::fill_pixels(
                        x + column * pixel_size,
                        y + row * pixel_size,
                        pixel_size,
                        pixel_size,
                        ink) && ok;
                }
            }
        }

        return ok;
    }
}

namespace tinyos::ui::font_logo
{
    bool draw_char(uint32_t x, uint32_t y, char character, renderer::Color ink, renderer::Color shadow, uint32_t pixel_size)
    {
        bool ok = draw_block_char(x + pixel_size, y + pixel_size, character, shadow, pixel_size);
        ok = draw_block_char(x, y, character, ink, pixel_size) && ok;
        return ok;
    }

    bool draw_text(uint32_t x, uint32_t y, const char* text, renderer::Color ink, renderer::Color shadow, uint32_t pixel_size)
    {
        if (text == nullptr)
        {
            return false;
        }

        bool ok = true;
        uint32_t cursor = x;
        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            ok = draw_char(cursor, y, text[index], ink, shadow, pixel_size) && ok;
            cursor += GlyphAdvance * pixel_size;
        }

        return ok;
    }

    uint32_t text_width(const char* text, uint32_t pixel_size)
    {
        if (text == nullptr)
        {
            return 0;
        }

        size_t length = 0;
        while (text[length] != '\0')
        {
            ++length;
        }

        return static_cast<uint32_t>(length) * GlyphAdvance * pixel_size;
    }

    bool validation_self_test()
    {
        return block_glyph_for('T') != nullptr && GlyphWidth == 10 && GlyphHeight == 14;
    }
}
