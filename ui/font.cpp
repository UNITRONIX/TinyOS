#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/font.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    bool fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, tinyos::ui::renderer::Color fill_color)
    {
        return tinyos::ui::renderer::fill_pixels(x, y, width, height, fill_color);
    }
}

namespace tinyos::ui::font
{
    const uint8_t* glyph_for(char character)
    {
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 32);
        }

        static constexpr uint8_t A[5] = { 0x7E, 0x09, 0x09, 0x09, 0x7E };
        static constexpr uint8_t B[5] = { 0x7F, 0x49, 0x49, 0x49, 0x36 };
        static constexpr uint8_t C[5] = { 0x3E, 0x41, 0x41, 0x41, 0x22 };
        static constexpr uint8_t D[5] = { 0x7F, 0x41, 0x41, 0x22, 0x1C };
        static constexpr uint8_t E[5] = { 0x7F, 0x49, 0x49, 0x49, 0x41 };
        static constexpr uint8_t F[5] = { 0x7F, 0x09, 0x09, 0x09, 0x01 };
        static constexpr uint8_t G[5] = { 0x3E, 0x41, 0x49, 0x49, 0x7A };
        static constexpr uint8_t H[5] = { 0x7F, 0x08, 0x08, 0x08, 0x7F };
        static constexpr uint8_t I[5] = { 0x00, 0x41, 0x7F, 0x41, 0x00 };
        static constexpr uint8_t J[5] = { 0x20, 0x40, 0x41, 0x3F, 0x01 };
        static constexpr uint8_t K[5] = { 0x7F, 0x08, 0x14, 0x22, 0x41 };
        static constexpr uint8_t L[5] = { 0x7F, 0x40, 0x40, 0x40, 0x40 };
        static constexpr uint8_t M[5] = { 0x7F, 0x02, 0x0C, 0x02, 0x7F };
        static constexpr uint8_t N[5] = { 0x7F, 0x04, 0x08, 0x10, 0x7F };
        static constexpr uint8_t O[5] = { 0x3E, 0x41, 0x41, 0x41, 0x3E };
        static constexpr uint8_t P[5] = { 0x7F, 0x09, 0x09, 0x09, 0x06 };
        static constexpr uint8_t Q[5] = { 0x3E, 0x41, 0x51, 0x21, 0x5E };
        static constexpr uint8_t R[5] = { 0x7F, 0x09, 0x19, 0x29, 0x46 };
        static constexpr uint8_t S[5] = { 0x46, 0x49, 0x49, 0x49, 0x31 };
        static constexpr uint8_t T[5] = { 0x01, 0x01, 0x7F, 0x01, 0x01 };
        static constexpr uint8_t U[5] = { 0x3F, 0x40, 0x40, 0x40, 0x3F };
        static constexpr uint8_t V[5] = { 0x1F, 0x20, 0x40, 0x20, 0x1F };
        static constexpr uint8_t W[5] = { 0x7F, 0x20, 0x18, 0x20, 0x7F };
        static constexpr uint8_t X[5] = { 0x63, 0x14, 0x08, 0x14, 0x63 };
        static constexpr uint8_t Y[5] = { 0x03, 0x04, 0x78, 0x04, 0x03 };
        static constexpr uint8_t Z[5] = { 0x61, 0x51, 0x49, 0x45, 0x43 };
        static constexpr uint8_t N0[5] = { 0x3E, 0x51, 0x49, 0x45, 0x3E };
        static constexpr uint8_t N1[5] = { 0x00, 0x42, 0x7F, 0x40, 0x00 };
        static constexpr uint8_t N2[5] = { 0x42, 0x61, 0x51, 0x49, 0x46 };
        static constexpr uint8_t N3[5] = { 0x21, 0x41, 0x45, 0x4B, 0x31 };
        static constexpr uint8_t N4[5] = { 0x18, 0x14, 0x12, 0x7F, 0x10 };
        static constexpr uint8_t N5[5] = { 0x27, 0x45, 0x45, 0x45, 0x39 };
        static constexpr uint8_t N6[5] = { 0x3C, 0x4A, 0x49, 0x49, 0x30 };
        static constexpr uint8_t N7[5] = { 0x01, 0x71, 0x09, 0x05, 0x03 };
        static constexpr uint8_t N8[5] = { 0x36, 0x49, 0x49, 0x49, 0x36 };
        static constexpr uint8_t N9[5] = { 0x06, 0x49, 0x49, 0x29, 0x1E };
        static constexpr uint8_t Colon[5] = { 0x00, 0x36, 0x36, 0x00, 0x00 };
        static constexpr uint8_t Dot[5] = { 0x00, 0x40, 0x60, 0x00, 0x00 };
        static constexpr uint8_t Dash[5] = { 0x08, 0x08, 0x08, 0x08, 0x08 };
        static constexpr uint8_t Slash[5] = { 0x20, 0x10, 0x08, 0x04, 0x02 };
        static constexpr uint8_t Greater[5] = { 0x41, 0x22, 0x14, 0x08, 0x00 };
        static constexpr uint8_t Dollar[5] = { 0x24, 0x2A, 0x7F, 0x2A, 0x12 };
        static constexpr uint8_t At[5] = { 0x3C, 0x42, 0x9D, 0xA1, 0x3C };
        static constexpr uint8_t BracketOpen[5] = { 0x00, 0x7E, 0x81, 0x81, 0x00 };
        static constexpr uint8_t BracketClose[5] = { 0x00, 0x81, 0x81, 0x7E, 0x00 };
        static constexpr uint8_t Star[5] = { 0x14, 0x08, 0x3E, 0x08, 0x14 };
        static constexpr uint8_t Space[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };

        switch (character)
        {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D; case 'E': return E;
        case 'F': return F; case 'G': return G; case 'H': return H; case 'I': return I; case 'J': return J;
        case 'K': return K; case 'L': return L; case 'M': return M; case 'N': return N; case 'O': return O;
        case 'P': return P; case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X; case 'Y': return Y;
        case 'Z': return Z; case '0': return N0; case '1': return N1; case '2': return N2; case '3': return N3;
        case '4': return N4; case '5': return N5; case '6': return N6; case '7': return N7; case '8': return N8;
        case '9': return N9; case ':': return Colon; case '.': return Dot; case '-': return Dash; case '_': return Dash;
        case '/': return Slash; case '>': return Greater; case '$': return Dollar; case '@': return At;
        case '[': return BracketOpen; case ']': return BracketClose; case '*': return Star; case ' ': return Space;
        default: return Space;
        }
    }

    bool draw_char(uint32_t x, uint32_t y, char character, renderer::Color ink, uint32_t pixel_size)
    {
        const uint8_t* glyph = glyph_for(character);
        bool ok = true;
        for (uint32_t column = 0; column < GlyphWidth; ++column)
        {
            for (uint32_t row = 0; row < GlyphHeight; ++row)
            {
                if ((glyph[column] & (1u << row)) != 0)
                {
                    ok = fill(x + column * pixel_size, y + row * pixel_size, pixel_size, pixel_size, ink) && ok;
                }
            }
        }

        return ok;
    }

    bool draw_text(uint32_t x, uint32_t y, const char* text, renderer::Color ink, uint32_t pixel_size)
    {
        if (text == nullptr)
        {
            return false;
        }

        bool ok = true;
        uint32_t cursor = x;
        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            ok = draw_char(cursor, y, text[index], ink, pixel_size) && ok;
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
        return glyph_for('A') != nullptr &&
            glyph_for('z') != nullptr &&
            glyph_for('@') != nullptr &&
            GlyphWidth == 5 &&
            GlyphHeight == 7 &&
            GlyphAdvance == 6;
    }
}
