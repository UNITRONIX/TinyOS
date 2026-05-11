#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/window_manager.hpp>

namespace
{
    constexpr size_t MaxWindows = 4;
    constexpr uint8_t DesktopAttribute = 0x17;
    constexpr uint8_t WindowAttribute = 0x1F;
    constexpr uint8_t FocusedWindowAttribute = 0x2F;
    constexpr uint8_t PanelAttribute = 0x70;

    tinyos::ui::window_manager::State g_state = {};
    tinyos::ui::window_manager::Window g_windows[MaxWindows] = {};
    uint64_t g_compositions = 0;
    uint64_t g_focus_changes = 0;
    uint64_t g_rejected_operations = 0;

    void set_window(size_t index, const char* title, tinyos::ui::window_manager::WindowRole role, uint32_t column, uint32_t row, uint32_t width, uint32_t height, bool focused)
    {
        g_windows[index].title = title;
        g_windows[index].role = role;
        g_windows[index].column = column;
        g_windows[index].row = row;
        g_windows[index].width = width;
        g_windows[index].height = height;
        g_windows[index].visible = true;
        g_windows[index].focused = focused;
    }

    bool draw_window_frame(const tinyos::ui::window_manager::Window& window)
    {
        if (!window.visible || window.width < 4 || window.height < 3)
        {
            return false;
        }

        const uint8_t attribute = window.focused ? FocusedWindowAttribute : WindowAttribute;
        bool ok = tinyos::ui::renderer::fill_rect(window.column, window.row, window.width, 1, '-', attribute);
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row + window.height - 1, window.width, 1, '-', attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row, 1, window.height, '|', attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column + window.width - 1, window.row, 1, window.height, '|', attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 2, window.row, window.title, attribute) && ok;
        return ok;
    }

    bool draw_panel_window(const tinyos::ui::window_manager::Window& window)
    {
        if (!window.visible || window.width < 4 || window.height == 0)
        {
            return false;
        }

        bool ok = tinyos::ui::renderer::fill_rect(window.column, window.row, window.width, window.height, ' ', PanelAttribute);
        ok = tinyos::ui::renderer::draw_text(window.column + 1, window.row, window.title, PanelAttribute) && ok;
        return ok;
    }

    bool draw_desktop()
    {
        const bool cleared = tinyos::ui::renderer::fill_rect(0, 0, g_state.columns, g_state.rows, ' ', DesktopAttribute);
        const bool title = tinyos::ui::renderer::draw_text(2, 0, "TinyOS GUI workspace", DesktopAttribute);
        const bool status = tinyos::ui::renderer::draw_text(2, g_state.rows - 1, "F6: cycle focus | shell: wmtest", PanelAttribute);
        return cleared && title && status;
    }
}

namespace tinyos::ui::window_manager
{
    void initialize()
    {
        const auto* renderer_state = tinyos::ui::renderer::state();
        if (renderer_state == nullptr || !renderer_state->ready || renderer_state->width < 40 || renderer_state->height < 15)
        {
            g_state.ready = false;
            g_state.columns = 0;
            g_state.rows = 0;
            g_state.window_count = 0;
            g_state.focused_index = 0;
            return;
        }

        g_state.ready = true;
        g_state.columns = renderer_state->width;
        g_state.rows = renderer_state->height;
        g_state.window_count = 3;
        g_state.focused_index = 1;

        set_window(0, "Desktop", WindowRole::Desktop, 0, 0, g_state.columns, g_state.rows, false);
        set_window(1, "System Shell", WindowRole::Shell, 3, 3, g_state.columns - 6, g_state.rows - 7, true);
        set_window(2, "Status", WindowRole::Panel, 2, g_state.rows - 3, g_state.columns - 4, 2, false);
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    size_t window_count()
    {
        return g_state.window_count;
    }

    const Window* window_at(size_t index)
    {
        if (index >= g_state.window_count)
        {
            return nullptr;
        }

        return &g_windows[index];
    }

    const Window* focused_window()
    {
        return window_at(g_state.focused_index);
    }

    bool focus_next()
    {
        if (!g_state.ready || g_state.window_count == 0)
        {
            ++g_rejected_operations;
            return false;
        }

        g_windows[g_state.focused_index].focused = false;
        g_state.focused_index = (g_state.focused_index + 1) % g_state.window_count;
        if (g_windows[g_state.focused_index].role == WindowRole::Desktop)
        {
            g_state.focused_index = (g_state.focused_index + 1) % g_state.window_count;
        }
        g_windows[g_state.focused_index].focused = true;
        ++g_focus_changes;
        return true;
    }

    bool compose()
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

        bool ok = draw_desktop();
        for (size_t index = 1; index < g_state.window_count; ++index)
        {
            if (g_windows[index].role == WindowRole::Panel)
            {
                ok = draw_panel_window(g_windows[index]) && ok;
            }
            else
            {
                ok = draw_window_frame(g_windows[index]) && ok;
            }
        }

        if (!ok)
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_compositions;
        return true;
    }

    bool render_demo()
    {
        if (!compose())
        {
            return false;
        }

        const bool shell_text = tinyos::ui::renderer::draw_text(6, 6, "TinyOS graphical shell scaffold", 0x1F);
        const bool hint = tinyos::ui::renderer::draw_text(6, 8, "Windows, focus and compositor are online.", 0x1F);
        return shell_text && hint;
    }

    uint64_t composition_count()
    {
        return g_compositions;
    }

    uint64_t focus_change_count()
    {
        return g_focus_changes;
    }

    uint64_t rejected_operation_count()
    {
        return g_rejected_operations;
    }

    bool validation_self_test()
    {
        return g_state.ready &&
            g_state.columns >= 40 &&
            g_state.rows >= 15 &&
            g_state.window_count == 3 &&
            focused_window() != nullptr &&
            focused_window()->role == WindowRole::Shell;
    }

    bool composition_validation_self_test()
    {
        return validation_self_test() &&
            g_windows[1].width >= 30 &&
            g_windows[1].height >= 8 &&
            g_windows[2].role == WindowRole::Panel &&
            g_windows[2].height == 2;
    }

    const char* role_name(WindowRole role)
    {
        switch (role)
        {
        case WindowRole::Desktop:
            return "desktop";
        case WindowRole::Shell:
            return "shell";
        case WindowRole::Panel:
            return "panel";
        case WindowRole::Dialog:
            return "dialog";
        }

        return "unknown";
    }
}