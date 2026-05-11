#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/graphical_desktop.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    constexpr size_t AppCount = 4;
    uint64_t g_renders = 0;
    uint64_t g_handled_keys = 0;
    uint64_t g_launches = 0;
    size_t g_selected_app = 0;
    size_t g_focused_app = 0;
    bool g_open_apps[AppCount] = {};

    tinyos::ui::renderer::Color color(uint8_t red, uint8_t green, uint8_t blue)
    {
        tinyos::ui::renderer::Color value;
        value.red = red;
        value.green = green;
        value.blue = blue;
        value.alpha = 0xFF;
        return value;
    }

    uint32_t scale(uint32_t value, uint32_t source, uint32_t target)
    {
        return target == 0 ? 0 : (value * source) / target;
    }

    bool fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, tinyos::ui::renderer::Color fill_color)
    {
        return tinyos::ui::renderer::fill_pixels(x, y, width, height, fill_color);
    }

    bool draw_frame(uint32_t x, uint32_t y, uint32_t width, uint32_t height, tinyos::ui::renderer::Color edge, tinyos::ui::renderer::Color body)
    {
        if (width < 2 || height < 2)
        {
            return false;
        }

        return fill(x, y, width, height, body) &&
            fill(x, y, width, 2, edge) &&
            fill(x, y + height - 2, width, 2, edge) &&
            fill(x, y, 2, height, edge) &&
            fill(x + width - 2, y, 2, height, edge);
    }

    bool draw_panel(const tinyos::ui::renderer::State* state)
    {
        const uint32_t panel_height = scale(34, state->height, 768);
        const uint32_t button_size = scale(24, state->height, 768);
        const uint32_t top = 0;
        const uint32_t pad = scale(8, state->width, 1024);
        const tinyos::ui::renderer::Color panel = color(27, 36, 48);
        const tinyos::ui::renderer::Color active = color(54, 134, 201);
        const tinyos::ui::renderer::Color light = color(216, 229, 238);

        bool ok = fill(0, top, state->width, panel_height, panel);
        for (uint32_t index = 0; index < 5; ++index)
        {
            const uint32_t left = pad + index * (button_size + pad);
            ok = ok && fill(left, top + 5, button_size, button_size, index == 0 ? active : color(43, 54, 70));
            ok = ok && fill(left + 7, top + 11, button_size - 14, button_size - 14, light);
        }

        const uint32_t clock_width = scale(68, state->width, 1024);
        ok = ok && fill(state->width - clock_width - pad, top + 7, clock_width, panel_height - 14, color(38, 48, 62));
        return ok;
    }

    bool draw_wallpaper(const tinyos::ui::renderer::State* state)
    {
        const uint32_t panel_height = scale(34, state->height, 768);
        bool ok = true;
        for (uint32_t row = panel_height; row < state->height; ++row)
        {
            const uint8_t blue = static_cast<uint8_t>(70 + ((row - panel_height) * 88) / (state->height - panel_height));
            const uint8_t green = static_cast<uint8_t>(86 + ((state->height - row) * 52) / state->height);
            const uint8_t red = static_cast<uint8_t>(12 + (row * 18) / state->height);
            ok = ok && fill(0, row, state->width, 1, color(red, green, blue));
        }

        const uint32_t glow_x = scale(290, state->width, 1024);
        const uint32_t glow_y = scale(170, state->height, 768);
        ok = ok && fill(glow_x, glow_y, scale(430, state->width, 1024), scale(14, state->height, 768), color(21, 112, 151));
        ok = ok && fill(glow_x + scale(80, state->width, 1024), glow_y + scale(70, state->height, 768), scale(420, state->width, 1024), scale(12, state->height, 768), color(20, 92, 128));
        return ok;
    }

    bool draw_icon(uint32_t x, uint32_t y, uint32_t size, tinyos::ui::renderer::Color base, tinyos::ui::renderer::Color accent, bool selected, bool open)
    {
        const tinyos::ui::renderer::Color edge = selected ? color(248, 210, 74) : color(232, 238, 244);
        bool ok = draw_frame(x, y, size, size, edge, base);
        ok = ok && fill(x + size / 4, y + size / 4, size / 2, size / 2, accent);
        if (open)
        {
            ok = ok && fill(x + size + scale(8, size, 42), y + size / 3, scale(8, size, 42), size / 3, color(113, 219, 132));
        }
        return ok;
    }

    bool draw_icons(const tinyos::ui::renderer::State* state)
    {
        const uint32_t icon_size = scale(42, state->height, 768);
        const uint32_t x = scale(28, state->width, 1024);
        uint32_t y = scale(58, state->height, 768);
        bool ok = draw_icon(x, y, icon_size, color(238, 241, 244), color(55, 66, 78), g_selected_app == 0, g_open_apps[0]);
        y += scale(96, state->height, 768);
        ok = ok && draw_icon(x, y, icon_size, color(242, 244, 247), color(69, 128, 207), g_selected_app == 1, g_open_apps[1]);
        y += scale(96, state->height, 768);
        ok = ok && draw_icon(x, y, icon_size, color(236, 240, 244), color(82, 153, 230), g_selected_app == 2, g_open_apps[2]);
        y += scale(96, state->height, 768);
        ok = ok && draw_icon(x, y, icon_size, color(240, 240, 234), color(185, 190, 196), g_selected_app == 3, g_open_apps[3]);
        return ok;
    }

    bool draw_terminal_window(const tinyos::ui::renderer::State* state, size_t app_index, bool focused)
    {
        const uint32_t x = scale(110 + static_cast<uint32_t>(app_index) * 34, state->width, 1024);
        const uint32_t y = scale(70 + static_cast<uint32_t>(app_index) * 28, state->height, 768);
        const uint32_t width = scale(805 - static_cast<uint32_t>(app_index) * 42, state->width, 1024);
        const uint32_t height = scale(520 - static_cast<uint32_t>(app_index) * 36, state->height, 768);
        const uint32_t title = scale(32, state->height, 768);
        const uint32_t menu = scale(28, state->height, 768);
        const tinyos::ui::renderer::Color chrome = focused ? color(36, 96, 148) : color(26, 35, 46);
        const tinyos::ui::renderer::Color dark = color(31 + static_cast<uint8_t>(app_index) * 4, 41 + static_cast<uint8_t>(app_index) * 4, 52 + static_cast<uint8_t>(app_index) * 4);
        const tinyos::ui::renderer::Color paper = color(232, 238, 244);
        const tinyos::ui::renderer::Color ink = color(214, 224, 230);
        bool ok = draw_frame(x, y, width, height, color(12, 18, 24), dark);
        ok = ok && fill(x + 2, y + 2, width - 4, title, chrome);
        ok = ok && fill(x + 2, y + title + 2, width - 4, menu, paper);
        ok = ok && fill(x + 2, y + title + menu + 2, width - 4, height - title - menu - 4, color(32, 43, 54));

        const uint32_t close_size = scale(14, state->height, 768);
        ok = ok && fill(x + width - scale(28, state->width, 1024), y + scale(9, state->height, 768), close_size, close_size, color(196, 72, 72));

        const uint32_t text_x = x + scale(34, state->width, 1024);
        uint32_t text_y = y + title + menu + scale(22, state->height, 768);
        for (uint32_t row = 0; row < 2; ++row)
        {
            ok = ok && fill(text_x, text_y, scale(180 + row * 58, state->width, 1024), scale(8, state->height, 768), ink);
            text_y += scale(28, state->height, 768);
        }

        const uint32_t bottom = y + height - scale(64, state->height, 768);
        for (uint32_t index = 0; index < 6; ++index)
        {
            const uint32_t left = x + scale(34 + index * 118, state->width, 1024);
            ok = ok && fill(left, bottom, scale(58, state->width, 1024), scale(18, state->height, 768), paper);
            ok = ok && fill(left + scale(66, state->width, 1024), bottom + scale(5, state->height, 768), scale(62, state->width, 1024), scale(8, state->height, 768), ink);
        }

        const uint32_t cursor_x = text_x + scale(236, state->width, 1024);
        const uint32_t cursor_y = y + title + menu + scale(48, state->height, 768);
        ok = ok && fill(cursor_x, cursor_y, scale(12, state->width, 1024), scale(24, state->height, 768), paper);
        return ok;
    }

    bool draw_open_windows(const tinyos::ui::renderer::State* state)
    {
        bool ok = true;
        bool any_open = false;
        for (size_t index = 0; index < AppCount; ++index)
        {
            if (g_open_apps[index])
            {
                any_open = true;
                ok = ok && draw_terminal_window(state, index, index == g_focused_app);
            }
        }

        if (!any_open)
        {
            ok = ok && fill(scale(155, state->width, 1024), scale(150, state->height, 768), scale(260, state->width, 1024), scale(18, state->height, 768), color(217, 227, 235));
            ok = ok && fill(scale(155, state->width, 1024), scale(184, state->height, 768), scale(210, state->width, 1024), scale(12, state->height, 768), color(151, 205, 228));
        }

        return ok;
    }

    bool focus_next_open_window()
    {
        for (size_t offset = 1; offset <= AppCount; ++offset)
        {
            const size_t candidate = (g_focused_app + offset) % AppCount;
            if (g_open_apps[candidate])
            {
                g_focused_app = candidate;
                g_selected_app = candidate;
                return true;
            }
        }

        return false;
    }
}

namespace tinyos::ui::graphical_desktop
{
    void initialize()
    {
        g_selected_app = 0;
        g_focused_app = 0;
        for (size_t index = 0; index < AppCount; ++index)
        {
            g_open_apps[index] = false;
        }
        g_open_apps[0] = true;
    }

    bool render()
    {
        if (!tinyos::ui::renderer::initialize_linear_framebuffer())
        {
            return false;
        }

        const auto* state = tinyos::ui::renderer::state();
        if (state == nullptr || !state->ready || !state->pixel_output || state->width < 320 || state->height < 200)
        {
            return false;
        }

        const bool ok = draw_wallpaper(state) &&
            draw_panel(state) &&
            draw_icons(state) &&
            draw_open_windows(state);
        if (ok)
        {
            ++g_renders;
        }

        return ok;
    }

    bool render_preview()
    {
        initialize();
        return render();
    }

    bool handle_key(char key)
    {
        bool handled = true;
        if (key == '\t' || key == 'n' || key == 's' || key == 'j')
        {
            g_selected_app = (g_selected_app + 1) % AppCount;
        }
        else if (key == 'p' || key == 'w' || key == 'k')
        {
            g_selected_app = g_selected_app == 0 ? AppCount - 1 : g_selected_app - 1;
        }
        else if (key == '\n' || key == ' ')
        {
            g_open_apps[g_selected_app] = true;
            g_focused_app = g_selected_app;
            ++g_launches;
        }
        else if (key == 'c' || key == 'x')
        {
            g_open_apps[g_focused_app] = false;
            (void)focus_next_open_window();
        }
        else if (key == 'f')
        {
            handled = focus_next_open_window();
        }
        else if (key == 'r')
        {
            handled = true;
        }
        else
        {
            handled = false;
        }

        if (handled)
        {
            ++g_handled_keys;
            return render();
        }

        return false;
    }

    bool run_session()
    {
        initialize();
        if (!render())
        {
            return false;
        }

        for (;;)
        {
            const char key = tinyos::drivers::keyboard::read_char();
            if (key == 27 || key == 'q' || key == 'Q')
            {
                return true;
            }

            (void)handle_key(key);
        }
    }

    uint64_t render_count()
    {
        return g_renders;
    }

    uint64_t handled_key_count()
    {
        return g_handled_keys;
    }

    uint64_t launch_count()
    {
        return g_launches;
    }

    bool validation_self_test()
    {
        initialize();
        const bool initial_state_valid = g_open_apps[0] && g_selected_app == 0 && g_focused_app == 0;
        g_selected_app = (g_selected_app + 1) % AppCount;
        const bool navigation_valid = g_selected_app == 1;
        g_open_apps[g_selected_app] = true;
        g_focused_app = g_selected_app;
        const bool launch_valid = g_open_apps[1] && g_focused_app == 1;
        return tinyos::ui::renderer::pack_color(color(1, 2, 3)) == 0xFF010203 &&
            initial_state_valid &&
            navigation_valid &&
            launch_valid;
    }
}