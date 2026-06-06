#include <stddef.h>
#include <stdint.h>

#include <tinyos/drivers/mouse.hpp>
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/font.hpp>
#include <tinyos/ui/graphical_desktop.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    constexpr size_t AppCount = 4;
    constexpr uint32_t MinWidth = 320;
    constexpr uint32_t MinHeight = 200;

    struct Rect
    {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
    };

    uint64_t g_renders = 0;
    uint64_t g_handled_keys = 0;
    uint64_t g_launches = 0;
    uint64_t g_pointer_events = 0;
    size_t g_selected_app = 0;
    size_t g_focused_app = 0;
    bool g_open_apps[AppCount] = {};
    bool g_window_initialized[AppCount] = {};
    uint32_t g_window_x[AppCount] = {};
    uint32_t g_window_y[AppCount] = {};
    uint32_t g_window_w[AppCount] = {};
    uint32_t g_window_h[AppCount] = {};
    uint32_t g_pointer_x = 120;
    uint32_t g_pointer_y = 120;
    bool g_dragging = false;
    size_t g_dragged_app = 0;
    uint32_t g_drag_offset_x = 0;
    uint32_t g_drag_offset_y = 0;

    const char* app_name(size_t index)
    {
        switch (index)
        {
        case 0: return "Terminal";
        case 1: return "Files";
        case 2: return "Browser";
        case 3: return "Settings";
        default: return "App";
        }
    }

    tinyos::ui::renderer::Color color(uint8_t red, uint8_t green, uint8_t blue)
    {
        tinyos::ui::renderer::Color value;
        value.red = red;
        value.green = green;
        value.blue = blue;
        value.alpha = 0xFF;
        return value;
    }

    uint32_t scale_x(uint32_t value, const tinyos::ui::renderer::State* state)
    {
        return state->width == 0 ? 0 : (value * state->width) / 1024;
    }

    uint32_t scale_y(uint32_t value, const tinyos::ui::renderer::State* state)
    {
        return state->height == 0 ? 0 : (value * state->height) / 768;
    }

    bool fill(uint32_t x, uint32_t y, uint32_t width, uint32_t height, tinyos::ui::renderer::Color fill_color)
    {
        return tinyos::ui::renderer::fill_pixels(x, y, width, height, fill_color);
    }

    bool contains(Rect rect, uint32_t x, uint32_t y)
    {
        return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
    }

    bool draw_text(uint32_t x, uint32_t y, const char* text, tinyos::ui::renderer::Color ink, uint32_t pixel_size)
    {
        return tinyos::ui::font::draw_text(x, y, text, ink, pixel_size);
    }

    bool draw_frame(Rect rect, tinyos::ui::renderer::Color edge, tinyos::ui::renderer::Color body)
    {
        if (rect.width < 2 || rect.height < 2)
        {
            return false;
        }

        return fill(rect.x, rect.y, rect.width, rect.height, body) &&
            fill(rect.x, rect.y, rect.width, 2, edge) &&
            fill(rect.x, rect.y + rect.height - 2, rect.width, 2, edge) &&
            fill(rect.x, rect.y, 2, rect.height, edge) &&
            fill(rect.x + rect.width - 2, rect.y, 2, rect.height, edge);
    }

    Rect dock_icon_rect(size_t index, const tinyos::ui::renderer::State* state)
    {
        Rect rect;
        rect.x = scale_x(11, state);
        rect.y = scale_y(70, state) + static_cast<uint32_t>(index) * scale_y(78, state);
        rect.width = scale_x(52, state);
        rect.height = scale_y(52, state);
        if (rect.width < 34) rect.width = 34;
        if (rect.height < 34) rect.height = 34;
        return rect;
    }

    Rect window_rect(size_t index)
    {
        Rect rect;
        rect.x = g_window_x[index];
        rect.y = g_window_y[index];
        rect.width = g_window_w[index];
        rect.height = g_window_h[index];
        return rect;
    }

    uint32_t title_height(const tinyos::ui::renderer::State* state)
    {
        uint32_t value = scale_y(34, state);
        return value < 22 ? 22 : value;
    }

    Rect close_rect(size_t index, const tinyos::ui::renderer::State* state)
    {
        const Rect window = window_rect(index);
        const uint32_t button = scale_y(16, state) < 10 ? 10 : scale_y(16, state);
        Rect rect;
        rect.x = window.x + window.width - button - scale_x(12, state);
        rect.y = window.y + (title_height(state) - button) / 2;
        rect.width = button;
        rect.height = button;
        return rect;
    }

    Rect title_rect(size_t index, const tinyos::ui::renderer::State* state)
    {
        const Rect window = window_rect(index);
        Rect rect;
        rect.x = window.x;
        rect.y = window.y;
        rect.width = window.width;
        rect.height = title_height(state);
        return rect;
    }

    void ensure_window_defaults(const tinyos::ui::renderer::State* state)
    {
        for (size_t index = 0; index < AppCount; ++index)
        {
            if (g_window_initialized[index])
            {
                continue;
            }

            g_window_x[index] = scale_x(120 + static_cast<uint32_t>(index) * 38, state);
            g_window_y[index] = scale_y(76 + static_cast<uint32_t>(index) * 30, state);
            g_window_w[index] = scale_x(760 - static_cast<uint32_t>(index) * 28, state);
            g_window_h[index] = scale_y(510 - static_cast<uint32_t>(index) * 22, state);
            if (g_window_w[index] < 230) g_window_w[index] = 230;
            if (g_window_h[index] < 145) g_window_h[index] = 145;
            g_window_initialized[index] = true;
        }
    }

    bool draw_wallpaper(const tinyos::ui::renderer::State* state)
    {
        bool ok = true;
        for (uint32_t row = 0; row < state->height; ++row)
        {
            const uint8_t red = static_cast<uint8_t>(28 + (row * 42) / state->height);
            const uint8_t green = static_cast<uint8_t>(42 + (row * 52) / state->height);
            const uint8_t blue = static_cast<uint8_t>(72 + (row * 82) / state->height);
            ok = fill(0, row, state->width, 1, color(red, green, blue)) && ok;
        }

        ok = fill(scale_x(230, state), scale_y(148, state), scale_x(470, state), scale_y(6, state), color(58, 152, 200)) && ok;
        ok = fill(scale_x(330, state), scale_y(224, state), scale_x(410, state), scale_y(4, state), color(39, 116, 171)) && ok;
        return ok;
    }

    bool draw_top_panel(const tinyos::ui::renderer::State* state)
    {
        const uint32_t panel = scale_y(34, state) < 24 ? 24 : scale_y(34, state);
        bool ok = fill(0, 0, state->width, panel, color(20, 26, 36));
        ok = draw_text(scale_x(16, state), scale_y(10, state), "TinyOS Plasma", color(234, 241, 247), 2) && ok;
        ok = draw_text(scale_x(232, state), scale_y(10, state), "Apps  Places  System", color(190, 204, 216), 2) && ok;
        ok = draw_text(state->width - scale_x(104, state), scale_y(10, state), "11:45", color(234, 241, 247), 2) && ok;
        return ok;
    }

    bool draw_dock_icon(Rect rect, size_t index)
    {
        const bool selected = g_selected_app == index;
        const bool open = g_open_apps[index];
        const tinyos::ui::renderer::Color edge = selected ? color(90, 182, 255) : color(80, 94, 112);
        const tinyos::ui::renderer::Color base = open ? color(36, 56, 76) : color(26, 36, 50);
        bool ok = draw_frame(rect, edge, base);
        const uint32_t pad = rect.width / 5;
        const tinyos::ui::renderer::Color accent = index == 0 ? color(111, 220, 139) : (index == 1 ? color(246, 183, 74) : (index == 2 ? color(94, 156, 238) : color(210, 126, 239)));
        ok = fill(rect.x + pad, rect.y + pad, rect.width - pad * 2, rect.height - pad * 2, accent) && ok;
        if (open)
        {
            ok = fill(rect.x + rect.width - 6, rect.y + rect.height / 2 - 4, 5, 8, color(97, 224, 130)) && ok;
        }
        return ok;
    }

    bool draw_dock(const tinyos::ui::renderer::State* state)
    {
        const uint32_t width = scale_x(76, state) < 54 ? 54 : scale_x(76, state);
        bool ok = fill(0, 0, width, state->height, color(17, 22, 32));
        for (size_t index = 0; index < AppCount; ++index)
        {
            const Rect icon = dock_icon_rect(index, state);
            ok = draw_dock_icon(icon, index) && ok;
        }

        return ok;
    }

    bool draw_taskbar(const tinyos::ui::renderer::State* state)
    {
        const uint32_t height = scale_y(42, state) < 28 ? 28 : scale_y(42, state);
        const uint32_t y = state->height - height;
        bool ok = fill(0, y, state->width, height, color(18, 24, 34));
        uint32_t x = scale_x(96, state);
        for (size_t index = 0; index < AppCount; ++index)
        {
            if (!g_open_apps[index])
            {
                continue;
            }

            const uint32_t width = scale_x(132, state) < 78 ? 78 : scale_x(132, state);
            const tinyos::ui::renderer::Color base = index == g_focused_app ? color(47, 98, 146) : color(34, 44, 58);
            ok = fill(x, y + 6, width, height - 12, base) && ok;
            ok = draw_text(x + 10, y + 13, app_name(index), color(231, 238, 244), 1) && ok;
            x += width + scale_x(10, state);
        }
        return ok;
    }

    bool draw_terminal_body(Rect window, const tinyos::ui::renderer::State* state)
    {
        const uint32_t title = title_height(state);
        const uint32_t y = window.y + title + scale_y(22, state);
        bool ok = draw_text(window.x + scale_x(28, state), y, "tinyos@desktop:~$ ls", color(119, 238, 150), 2);
        ok = draw_text(window.x + scale_x(28, state), y + scale_y(34, state), "apps  docs  devices  system", color(214, 224, 230), 2) && ok;
        ok = draw_text(window.x + scale_x(28, state), y + scale_y(76, state), "tinyos@desktop:~$ run app", color(119, 238, 150), 2) && ok;
        ok = fill(window.x + scale_x(292, state), y + scale_y(78, state), scale_x(12, state), scale_y(22, state), color(228, 235, 240)) && ok;
        return ok;
    }

    bool draw_files_body(Rect window, const tinyos::ui::renderer::State* state)
    {
        const uint32_t title = title_height(state);
        bool ok = fill(window.x + 2, window.y + title, scale_x(150, state), window.height - title - 2, color(42, 50, 64));
        ok = draw_text(window.x + scale_x(20, state), window.y + title + scale_y(24, state), "Home", color(238, 242, 246), 2) && ok;
        ok = draw_text(window.x + scale_x(20, state), window.y + title + scale_y(58, state), "Apps", color(170, 184, 199), 2) && ok;
        ok = draw_text(window.x + scale_x(20, state), window.y + title + scale_y(92, state), "System", color(170, 184, 199), 2) && ok;
        for (uint32_t item = 0; item < 4; ++item)
        {
            const uint32_t x = window.x + scale_x(190 + item * 116, state);
            const uint32_t y = window.y + title + scale_y(34, state);
            ok = draw_frame({ x, y, scale_x(74, state), scale_y(68, state) }, color(188, 202, 216), color(222, 230, 238)) && ok;
        }
        ok = draw_text(window.x + scale_x(190, state), window.y + title + scale_y(128, state), "kernel  docs  tapp  keys", color(55, 66, 78), 2) && ok;
        return ok;
    }

    bool draw_browser_body(Rect window, const tinyos::ui::renderer::State* state)
    {
        const uint32_t title = title_height(state);
        bool ok = fill(window.x + scale_x(22, state), window.y + title + scale_y(18, state), window.width - scale_x(44, state), scale_y(28, state), color(232, 238, 244));
        ok = draw_text(window.x + scale_x(36, state), window.y + title + scale_y(26, state), "https://tinyos.local", color(45, 58, 72), 1) && ok;
        ok = draw_text(window.x + scale_x(42, state), window.y + title + scale_y(86, state), "TinyOS Control Center", color(232, 238, 244), 2) && ok;
        ok = fill(window.x + scale_x(42, state), window.y + title + scale_y(130, state), scale_x(220, state), scale_y(86, state), color(50, 86, 122)) && ok;
        ok = fill(window.x + scale_x(286, state), window.y + title + scale_y(130, state), scale_x(220, state), scale_y(86, state), color(67, 104, 84)) && ok;
        ok = draw_text(window.x + scale_x(62, state), window.y + title + scale_y(160, state), "Devices", color(232, 238, 244), 2) && ok;
        ok = draw_text(window.x + scale_x(306, state), window.y + title + scale_y(160, state), "Security", color(232, 238, 244), 2) && ok;
        return ok;
    }

    bool draw_settings_body(Rect window, const tinyos::ui::renderer::State* state)
    {
        const uint32_t title = title_height(state);
        bool ok = draw_text(window.x + scale_x(28, state), window.y + title + scale_y(28, state), "System Settings", color(232, 238, 244), 2);
        for (uint32_t row = 0; row < 2; ++row)
        {
            for (uint32_t col = 0; col < 3; ++col)
            {
                const uint32_t x = window.x + scale_x(30 + col * 168, state);
                const uint32_t y = window.y + title + scale_y(78 + row * 96, state);
                ok = draw_frame({ x, y, scale_x(138, state), scale_y(72, state) }, color(76, 96, 118), color(42, 54, 68)) && ok;
            }
        }
        ok = draw_text(window.x + scale_x(48, state), window.y + title + scale_y(105, state), "Display", color(220, 230, 238), 1) && ok;
        ok = draw_text(window.x + scale_x(216, state), window.y + title + scale_y(105, state), "Input", color(220, 230, 238), 1) && ok;
        ok = draw_text(window.x + scale_x(384, state), window.y + title + scale_y(105, state), "Users", color(220, 230, 238), 1) && ok;
        return ok;
    }

    bool draw_window(size_t index, const tinyos::ui::renderer::State* state)
    {
        const Rect window = window_rect(index);
        const uint32_t title = title_height(state);
        const tinyos::ui::renderer::Color edge = index == g_focused_app ? color(94, 178, 238) : color(42, 54, 68);
        bool ok = draw_frame(window, edge, color(28, 36, 48));
        ok = fill(window.x + 2, window.y + 2, window.width - 4, title - 2, index == g_focused_app ? color(35, 95, 150) : color(31, 42, 56)) && ok;
        ok = draw_text(window.x + scale_x(16, state), window.y + scale_y(11, state), app_name(index), color(238, 244, 248), 2) && ok;
        const Rect close = close_rect(index, state);
        ok = fill(close.x, close.y, close.width, close.height, color(206, 78, 78)) && ok;
        ok = draw_text(close.x + 3, close.y + 3, "X", color(35, 24, 24), 1) && ok;
        ok = fill(window.x + 2, window.y + title, window.width - 4, window.height - title - 2, color(30, 40, 52)) && ok;

        if (index == 0) ok = draw_terminal_body(window, state) && ok;
        else if (index == 1) ok = draw_files_body(window, state) && ok;
        else if (index == 2) ok = draw_browser_body(window, state) && ok;
        else ok = draw_settings_body(window, state) && ok;
        return ok;
    }

    bool draw_windows(const tinyos::ui::renderer::State* state)
    {
        bool ok = true;
        bool any_open = false;
        for (size_t index = 0; index < AppCount; ++index)
        {
            if (g_open_apps[index] && index != g_focused_app)
            {
                any_open = true;
                ok = draw_window(index, state) && ok;
            }
        }
        if (g_open_apps[g_focused_app])
        {
            any_open = true;
            ok = draw_window(g_focused_app, state) && ok;
        }
        if (!any_open)
        {
            ok = draw_text(scale_x(160, state), scale_y(160, state), "Open an app from the dock", color(226, 235, 242), 2) && ok;
        }
        return ok;
    }

    bool draw_cursor()
    {
        bool ok = true;
        const tinyos::ui::renderer::Color white = color(245, 248, 252);
        const tinyos::ui::renderer::Color black = color(10, 12, 14);
        for (uint32_t row = 0; row < 18; ++row)
        {
            const uint32_t width = row < 10 ? row + 1 : 6;
            ok = fill(g_pointer_x, g_pointer_y + row, width, 1, black) && ok;
            if (width > 2)
            {
                ok = fill(g_pointer_x + 1, g_pointer_y + row, width - 2, 1, white) && ok;
            }
        }
        ok = fill(g_pointer_x + 6, g_pointer_y + 12, 8, 3, black) && ok;
        ok = fill(g_pointer_x + 7, g_pointer_y + 12, 5, 2, white) && ok;
        return ok;
    }

    void open_app(size_t index)
    {
        if (index >= AppCount)
        {
            return;
        }

        if (!g_open_apps[index])
        {
            ++g_launches;
        }
        g_open_apps[index] = true;
        g_selected_app = index;
        g_focused_app = index;
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

    bool handle_mouse_button(const tinyos::ui::events::Event& event, const tinyos::ui::renderer::State* state)
    {
        if (event.button != 1)
        {
            return false;
        }

        if (!event.pressed)
        {
            g_dragging = false;
            return true;
        }

        for (size_t index = 0; index < AppCount; ++index)
        {
            if (contains(dock_icon_rect(index, state), event.column, event.row))
            {
                open_app(index);
                return true;
            }
        }

        for (size_t reverse = 0; reverse < AppCount; ++reverse)
        {
            const size_t index = (g_focused_app + AppCount - reverse) % AppCount;
            if (!g_open_apps[index])
            {
                continue;
            }

            if (contains(close_rect(index, state), event.column, event.row))
            {
                g_open_apps[index] = false;
                g_dragging = false;
                (void)focus_next_open_window();
                return true;
            }

            if (contains(title_rect(index, state), event.column, event.row))
            {
                g_focused_app = index;
                g_selected_app = index;
                g_dragging = true;
                g_dragged_app = index;
                g_drag_offset_x = event.column - g_window_x[index];
                g_drag_offset_y = event.row - g_window_y[index];
                return true;
            }

            if (contains(window_rect(index), event.column, event.row))
            {
                g_focused_app = index;
                g_selected_app = index;
                return true;
            }
        }

        return true;
    }

    bool handle_pointer(const tinyos::ui::events::Event& event, const tinyos::ui::renderer::State* state)
    {
        g_pointer_x = event.column;
        g_pointer_y = event.row;
        ++g_pointer_events;
        if (g_dragging && g_dragged_app < AppCount)
        {
            const uint32_t panel = title_height(state);
            uint32_t x = event.column > g_drag_offset_x ? event.column - g_drag_offset_x : 0;
            uint32_t y = event.row > g_drag_offset_y ? event.row - g_drag_offset_y : panel;
            if (x + g_window_w[g_dragged_app] >= state->width)
            {
                x = state->width > g_window_w[g_dragged_app] ? state->width - g_window_w[g_dragged_app] - 1 : 0;
            }
            if (y + g_window_h[g_dragged_app] >= state->height)
            {
                y = state->height > g_window_h[g_dragged_app] ? state->height - g_window_h[g_dragged_app] - 1 : panel;
            }
            g_window_x[g_dragged_app] = x;
            g_window_y[g_dragged_app] = y;
        }

        return true;
    }

    bool handle_event(const tinyos::ui::events::Event& event)
    {
        const auto* state = tinyos::ui::renderer::state();
        if (state == nullptr || !state->ready || !state->pixel_output)
        {
            return false;
        }

        if (event.type == tinyos::ui::events::EventType::Key && event.pressed)
        {
            if (event.character == 27 || event.character == 'q' || event.character == 'Q')
            {
                return false;
            }

            return tinyos::ui::graphical_desktop::handle_key(event.character);
        }

        if (event.type == tinyos::ui::events::EventType::Pointer)
        {
            return handle_pointer(event, state) && tinyos::ui::graphical_desktop::render();
        }

        if (event.type == tinyos::ui::events::EventType::MouseButton)
        {
            return handle_mouse_button(event, state) && tinyos::ui::graphical_desktop::render();
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
        g_pointer_x = tinyos::drivers::mouse::cursor_x();
        g_pointer_y = tinyos::drivers::mouse::cursor_y();
        g_dragging = false;
        g_dragged_app = 0;
        g_drag_offset_x = 0;
        g_drag_offset_y = 0;
        for (size_t index = 0; index < AppCount; ++index)
        {
            g_open_apps[index] = false;
            g_window_initialized[index] = false;
            g_window_x[index] = 0;
            g_window_y[index] = 0;
            g_window_w[index] = 0;
            g_window_h[index] = 0;
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
        if (state == nullptr || !state->ready || !state->pixel_output || state->width < MinWidth || state->height < MinHeight)
        {
            return false;
        }

        tinyos::drivers::mouse::set_bounds(state->width, state->height);
        if (tinyos::drivers::mouse::is_ready())
        {
            g_pointer_x = tinyos::drivers::mouse::cursor_x();
            g_pointer_y = tinyos::drivers::mouse::cursor_y();
        }
        ensure_window_defaults(state);

        const bool ok = draw_wallpaper(state) &&
            draw_top_panel(state) &&
            draw_dock(state) &&
            draw_windows(state) &&
            draw_taskbar(state) &&
            draw_cursor();
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
            open_app(g_selected_app);
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
            tinyos::ui::events::pump_from_input(16);
            bool processed = false;
            tinyos::ui::events::Event event;
            event.type = tinyos::ui::events::EventType::None;
            event.source = tinyos::ui::events::Source::None;
            event.character = 0;
            event.pressed = false;
            event.column = 0;
            event.row = 0;
            event.delta_column = 0;
            event.delta_row = 0;
            event.button = 0;
            event.sequence = 0;

            while (tinyos::ui::events::poll_event(event))
            {
                if (event.type == tinyos::ui::events::EventType::Key && event.pressed && (event.character == 27 || event.character == 'q' || event.character == 'Q'))
                {
                    return true;
                }

                (void)handle_event(event);
                processed = true;
            }

            if (!processed)
            {
                asm volatile ("hlt");
            }
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
        open_app(g_selected_app);
        const bool launch_valid = g_open_apps[1] && g_focused_app == 1 && g_launches > 0;
        return tinyos::ui::renderer::pack_color(color(1, 2, 3)) == 0xFF010203 &&
            initial_state_valid &&
            navigation_valid &&
            launch_valid &&
            tinyos::ui::font::glyph_for('A') != nullptr;
    }
}
