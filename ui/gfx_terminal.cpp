#include <stddef.h>
#include <stdint.h>

#include <tinyos/config.hpp>
#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/shell/shell.hpp>
#include <tinyos/ui/font_atlas.hpp>
#include <tinyos/ui/font_logo.hpp>
#include <tinyos/ui/gfx_anim.hpp>
#include <tinyos/ui/gfx_console.hpp>
#include <tinyos/ui/gfx_input.hpp>
#include <tinyos/ui/gfx_picker.hpp>
#include <tinyos/ui/gfx_scrollback.hpp>
#include <tinyos/ui/gfx_terminal.hpp>
#include <tinyos/ui/gfx_theme.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    constexpr uint32_t MinWidth = 640;
    constexpr uint32_t MinHeight = 480;
    constexpr uint32_t ReferenceWidth = 1024;
    constexpr uint32_t ReferenceHeight = 768;
    constexpr size_t OutputViewportLines = 10;

    uint64_t g_renders = 0;
    uint64_t g_handled_keys = 0;
    uint64_t g_commands = 0;
    char g_working_directory[96] = "/";
    tinyos::ui::gfx_input::State g_input = {};

    uint32_t scale_x(uint32_t value, const tinyos::ui::renderer::State* state)
    {
        return state->width == 0 ? 0 : (value * state->width) / ReferenceWidth;
    }

    uint32_t scale_y(uint32_t value, const tinyos::ui::renderer::State* state)
    {
        return state->height == 0 ? 0 : (value * state->height) / ReferenceHeight;
    }

    void copy_text(char* destination, size_t destination_size, const char* source)
    {
        if (destination == nullptr || destination_size == 0)
        {
            return;
        }

        (void)tinyos::core::memory::string_copy_safe(destination, destination_size, source != nullptr ? source : "");
    }

    bool draw_mascot(uint32_t x, uint32_t y, uint32_t pixel_size, const tinyos::ui::gfx_theme::Theme& theme, uint32_t frame)
    {
        static constexpr uint16_t OpenSprite[16] = {
            0b0000011111100000, 0b0001111111111000, 0b0011111111111100, 0b0111111111111110,
            0b1110011111110011, 0b1100111111110011, 0b1100111111110011, 0b1111111111111111,
            0b1111111111111111, 0b1111111111111111, 0b1111110110111111, 0b1111110110111111,
            0b1111111111111111, 0b0111111111111110, 0b0011111111111100, 0b0001111111111000
        };
        static constexpr uint16_t BlinkSprite[16] = {
            0b0000011111100000, 0b0001111111111000, 0b0011111111111100, 0b0111111111111110,
            0b1111111111111111, 0b1111111111111111, 0b1111111111111111, 0b1111111111111111,
            0b1111111111111111, 0b1111111111111111, 0b1111110110111111, 0b1111110110111111,
            0b1111111111111111, 0b0111111111111110, 0b0011111111111100, 0b0001111111111000
        };

        const uint16_t* sprite = frame == 0 ? OpenSprite : BlinkSprite;
        bool ok = true;
        for (uint32_t row = 0; row < 16; ++row)
        {
            for (uint32_t column = 0; column < 16; ++column)
            {
                if ((sprite[row] & (1u << (15 - column))) == 0)
                {
                    continue;
                }

                tinyos::ui::renderer::Color ink = theme.mascot_outline;
                if (frame == 0 && row >= 4 && row <= 6 && (column == 5 || column == 6 || column == 9 || column == 10))
                {
                    ink = theme.mascot_eye;
                }
                else if (row >= 10 && row <= 11 && (column == 7 || column == 8))
                {
                    ink = theme.mascot_mouth;
                }

                ok = tinyos::ui::renderer::fill_pixels(
                    x + column * pixel_size,
                    y + row * pixel_size,
                    pixel_size,
                    pixel_size,
                    ink) && ok;
            }
        }

        return ok;
    }

    bool draw_input_box(
        uint32_t x,
        uint32_t y,
        uint32_t width,
        uint32_t height,
        const tinyos::ui::gfx_theme::Theme& theme,
        uint64_t ticks)
    {
        const uint32_t border = scale_y(2, tinyos::ui::renderer::state());
        bool ok = tinyos::ui::renderer::fill_pixels(x, y, width, height, theme.background);
        ok = tinyos::ui::renderer::fill_pixels(x, y, width, border, theme.border) && ok;
        ok = tinyos::ui::renderer::fill_pixels(x, y + height - border, width, border, theme.border) && ok;

        const uint32_t padding = scale_x(16, tinyos::ui::renderer::state());
        const uint32_t text_y = y + (height / 2) - (tinyos::ui::font_atlas::GlyphHeight / 2);
        const char* line = tinyos::ui::gfx_input::current_line(&g_input);

        if (g_input.length == 0)
        {
            ok = tinyos::ui::font_atlas::draw_text(
                x + padding,
                text_y,
                "Type @ to mention files or / for commands",
                theme.dim,
                tinyos::ui::font_atlas::Style::Smooth) && ok;
        }
        else
        {
            ok = tinyos::ui::font_atlas::draw_text(
                x + padding,
                text_y,
                line,
                theme.foreground,
                tinyos::ui::font_atlas::Style::Normal) && ok;
        }

        const float opacity = tinyos::ui::gfx_anim::cursor_opacity(ticks);
        if (opacity > 0.5f)
        {
            char prefix_buffer[tinyos::ui::gfx_input::MaxLineLength + 1];
            prefix_buffer[0] = '\0';
            for (size_t index = 0; index < g_input.cursor && index < tinyos::ui::gfx_input::MaxLineLength; ++index)
            {
                prefix_buffer[index] = g_input.buffer[index];
                prefix_buffer[index + 1] = '\0';
            }

            const uint32_t cursor_x = x + padding + tinyos::ui::font_atlas::text_width(prefix_buffer, tinyos::ui::font_atlas::Style::Normal);
            ok = tinyos::ui::renderer::fill_pixels(cursor_x, text_y, 2, tinyos::ui::font_atlas::GlyphHeight, theme.cursor) && ok;
        }

        return ok;
    }

    bool draw_output_region(uint32_t x, uint32_t y, uint32_t width, const tinyos::ui::gfx_theme::Theme& theme)
    {
        const size_t start = tinyos::ui::gfx_scrollback::visible_start(OutputViewportLines);
        const size_t count = tinyos::ui::gfx_scrollback::visible_count(OutputViewportLines);
        bool ok = true;
        uint32_t row_y = y;
        for (size_t index = 0; index < count; ++index)
        {
            const char* line = tinyos::ui::gfx_scrollback::line_at(start + index);
            ok = tinyos::ui::font_atlas::draw_text(
                x,
                row_y,
                line,
                theme.output,
                tinyos::ui::font_atlas::Style::Normal) && ok;
            row_y += tinyos::ui::font_atlas::GlyphHeight + 4;
            if (row_y + tinyos::ui::font_atlas::GlyphHeight > y + scale_y(220, tinyos::ui::renderer::state()))
            {
                break;
            }

            (void)width;
        }

        return ok;
    }

    bool draw_scene(const tinyos::ui::renderer::State* state, const tinyos::ui::gfx_theme::Theme& theme, uint64_t ticks)
    {
        const uint32_t progress = tinyos::ui::gfx_anim::intro_progress(ticks);
        const uint32_t margin_x = scale_x(48, state);
        const uint32_t margin_y = scale_y(36, state);
        const uint32_t logo_scale = state->width >= 1024 ? 4 : 3;
        const uint8_t logo_alpha = tinyos::ui::gfx_anim::logo_alpha(ticks);

        bool ok = tinyos::ui::renderer::fill_pixels(0, 0, state->width, state->height, theme.background);

        if (progress >= 10)
        {
            char welcome[32];
            copy_text(welcome, sizeof(welcome), "Welcome to");
            const size_t visible = tinyos::ui::gfx_anim::typewriter_length(welcome, ticks, 18);
            welcome[visible] = '\0';
            ok = tinyos::ui::font_atlas::draw_text(
                margin_x,
                margin_y,
                welcome,
                theme.foreground,
                tinyos::ui::font_atlas::Style::Normal) && ok;
        }

        if (progress >= 25 && logo_alpha > 0)
        {
            const uint32_t logo_y = margin_y + scale_y(28, state);
            tinyos::ui::renderer::Color accent = theme.accent;
            accent.alpha = logo_alpha;
            tinyos::ui::renderer::Color shadow = theme.accent_shadow;
            shadow.alpha = logo_alpha;
            ok = tinyos::ui::font_logo::draw_text(margin_x, logo_y, "TINYOS", accent, shadow, logo_scale) && ok;
            ok = draw_mascot(
                margin_x + scale_x(360, state),
                logo_y - scale_y(8, state),
                static_cast<uint32_t>(logo_scale),
                theme,
                tinyos::ui::gfx_anim::mascot_frame(ticks)) && ok;
        }

        if (progress >= 45)
        {
            char version_line[64];
            copy_text(version_line, sizeof(version_line), "CLI Version ");
            size_t prefix_length = tinyos::core::string::length(version_line);
            (void)tinyos::core::memory::string_copy_safe(
                version_line + prefix_length,
                sizeof(version_line) - prefix_length,
                tinyos::config::Version);
            ok = tinyos::ui::font_atlas::draw_text(
                margin_x,
                margin_y + scale_y(120, state),
                version_line,
                theme.foreground,
                tinyos::ui::font_atlas::Style::Normal) && ok;
        }

        if (progress >= 60)
        {
            char path_line[128];
            copy_text(path_line, sizeof(path_line), g_working_directory);
            size_t path_length = tinyos::core::string::length(path_line);
            (void)tinyos::core::memory::string_copy_safe(
                path_line + path_length,
                sizeof(path_line) - path_length,
                "  [main*]");
            ok = tinyos::ui::font_atlas::draw_text(
                margin_x,
                margin_y + scale_y(190, state),
                path_line,
                theme.foreground,
                tinyos::ui::font_atlas::Style::Normal) && ok;
        }

        if (progress >= 75)
        {
            const uint32_t box_x = margin_x;
            const uint32_t box_y = margin_y + scale_y(240, state);
            const uint32_t box_w = state->width - (margin_x * 2);
            const uint32_t box_h = scale_y(56, state);
            ok = draw_input_box(box_x, box_y, box_w, box_h, theme, ticks) && ok;
            ok = tinyos::ui::gfx_picker::draw(box_x, box_y + box_h + 8, box_w, scale_y(160, state), theme, 1) && ok;
        }

        if (progress >= 90)
        {
            ok = tinyos::ui::font_atlas::draw_text(
                margin_x,
                state->height - scale_y(36, state),
                "Shift+Tab cycle mode  |  PgUp/PgDn scroll  |  Q exit",
                theme.dim,
                tinyos::ui::font_atlas::Style::Normal) && ok;
        }

        if (tinyos::ui::gfx_scrollback::line_count() > 0)
        {
            ok = draw_output_region(margin_x, margin_y + scale_y(320, state), state->width - (margin_x * 2), theme) && ok;
        }

        return ok;
    }

    void execute_command()
    {
        ++g_commands;
        if (g_input.length == 0)
        {
            return;
        }

        if (tinyos::core::string::compare(g_input.buffer, "exit") == 0 ||
            tinyos::core::string::compare(g_input.buffer, "quit") == 0)
        {
            return;
        }

        tinyos::ui::gfx_scrollback::append_text("tinyos> ");
        tinyos::ui::gfx_scrollback::append_line(g_input.buffer);
        tinyos::shell::execute(g_input.buffer);
        tinyos::ui::gfx_scrollback::append_char('\n');
        tinyos::ui::gfx_scrollback::scroll_to_bottom();
    }

    bool handle_key(char key)
    {
        if (key == tinyos::drivers::keyboard::KeyShiftTab)
        {
            tinyos::ui::gfx_picker::cycle_mode();
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyPgUp)
        {
            tinyos::ui::gfx_scrollback::scroll_up(1);
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyPgDn)
        {
            tinyos::ui::gfx_scrollback::scroll_down(1);
            return true;
        }

        if (key == '\n')
        {
            if (g_input.length > 0 &&
                (tinyos::core::string::compare(g_input.buffer, "exit") == 0 ||
                    tinyos::core::string::compare(g_input.buffer, "quit") == 0))
            {
                return false;
            }

            (void)tinyos::ui::gfx_input::handle_key(&g_input, key);
            execute_command();
            tinyos::ui::gfx_input::reset(&g_input);
            tinyos::ui::gfx_picker::initialize();
            return true;
        }

        const bool handled = tinyos::ui::gfx_input::handle_key(&g_input, key);
        tinyos::ui::gfx_picker::update_for_input(g_input.buffer, g_input.cursor, g_working_directory);
        return handled;
    }
}

namespace tinyos::ui::gfx_terminal
{
    void initialize()
    {
        tinyos::ui::gfx_theme::initialize();
        tinyos::ui::gfx_scrollback::initialize();
        tinyos::ui::gfx_input::initialize(&g_input);
        tinyos::ui::gfx_picker::initialize();
        tinyos::ui::gfx_anim::reset_session(tinyos::drivers::pit::ticks());
    }

    bool render()
    {
        if (!tinyos::ui::renderer::initialize_linear_framebuffer())
        {
            return false;
        }

        const auto* state = tinyos::ui::renderer::state();
        if (state == nullptr || !state->ready || !state->pixel_output || state->width < MinWidth || state->height < MinHeight)
        {
            return false;
        }

        const auto* theme = tinyos::ui::gfx_theme::active();
        if (theme == nullptr)
        {
            return false;
        }

        const bool ok = draw_scene(state, *theme, tinyos::drivers::pit::ticks());
        if (ok)
        {
            ++g_renders;
        }

        return ok;
    }

    bool run_session(const char* working_directory)
    {
        if (working_directory != nullptr)
        {
            copy_text(g_working_directory, sizeof(g_working_directory), working_directory);
        }

        initialize();
        tinyos::ui::gfx_console::begin_session();
        if (!render())
        {
            tinyos::ui::gfx_console::end_session();
            return false;
        }

        uint64_t last_anim_tick = tinyos::drivers::pit::ticks();
        for (;;)
        {
            if (tinyos::drivers::keyboard::buffered_character_count() > 0)
            {
                const char character = tinyos::drivers::keyboard::read_char();
                if (character == 27 || character == 'q' || character == 'Q')
                {
                    break;
                }

                ++g_handled_keys;
                if (!handle_key(character))
                {
                    break;
                }

                if (!render())
                {
                    tinyos::ui::gfx_console::end_session();
                    return false;
                }

                last_anim_tick = tinyos::drivers::pit::ticks();
                continue;
            }

            const uint64_t ticks = tinyos::drivers::pit::ticks();
            const bool intro_animating = !tinyos::ui::gfx_anim::intro_complete() && ticks != last_anim_tick;
            const bool cursor_phase = tinyos::ui::gfx_anim::intro_complete() &&
                ((ticks / 15) != (last_anim_tick / 15));
            const bool mascot_phase = (tinyos::ui::gfx_anim::mascot_frame(ticks) != tinyos::ui::gfx_anim::mascot_frame(last_anim_tick));
            if (intro_animating || cursor_phase || mascot_phase)
            {
                if (!render())
                {
                    tinyos::ui::gfx_console::end_session();
                    return false;
                }

                last_anim_tick = ticks;
            }

            asm volatile ("hlt");
        }

        tinyos::ui::gfx_console::end_session();
        return true;
    }

    uint64_t render_count()
    {
        return g_renders;
    }

    uint64_t handled_key_count()
    {
        return g_handled_keys;
    }

    uint64_t command_count()
    {
        return g_commands;
    }

    bool validation_self_test()
    {
        return tinyos::ui::font_atlas::validation_self_test() &&
            tinyos::ui::font_logo::validation_self_test() &&
            tinyos::ui::gfx_input::validation_self_test() &&
            tinyos::ui::gfx_scrollback::validation_self_test() &&
            tinyos::ui::gfx_theme::validation_self_test() &&
            tinyos::ui::gfx_anim::validation_self_test();
    }
}
