#include <tinyos/ui/desktop.hpp>
#include <tinyos/ui/cursor.hpp>
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/window_manager.hpp>

namespace
{
    constexpr size_t MaxLauncherItems = 4;
    constexpr size_t MaxAppWindows = 3;
    constexpr uint8_t WallpaperAttribute = 0x17;
    constexpr uint8_t TopPanelAttribute = 0x1F;
    constexpr uint8_t MenuAttribute = 0x70;
    constexpr uint8_t IconAttribute = 0x1E;
    constexpr uint8_t SelectedIconAttribute = 0x2E;
    constexpr uint8_t AppWindowAttribute = 0x1F;
    constexpr uint8_t FocusedAppWindowAttribute = 0x2F;
    constexpr uint8_t StatusAttribute = 0x70;
    constexpr uint32_t IconWidth = 14;
    constexpr uint32_t IconHeight = 3;
    constexpr uint32_t FullscreenIconColumn = 2;
    constexpr uint32_t FullscreenIconFirstRow = 3;

    tinyos::ui::desktop::State g_state = {};
    tinyos::ui::desktop::LauncherItem g_items[MaxLauncherItems] = {};
    tinyos::ui::desktop::DesktopIcon g_icons[MaxLauncherItems] = {};
    tinyos::ui::desktop::AppWindow g_app_windows[MaxAppWindows] = {};
    uint64_t g_renders = 0;
    uint64_t g_selection_changes = 0;
    uint64_t g_launch_requests = 0;
    uint64_t g_handled_events = 0;
    uint64_t g_pointer_events = 0;
    uint64_t g_rejected_operations = 0;

    void set_launcher_item(size_t index, const char* title, const char* command, bool enabled)
    {
        g_items[index].title = title;
        g_items[index].command = command;
        g_items[index].enabled = enabled;
    }

    void set_icon(size_t index, const char* title, const char* command, uint32_t column, uint32_t row, bool selected)
    {
        g_icons[index].title = title;
        g_icons[index].command = command;
        g_icons[index].column = column;
        g_icons[index].row = row;
        g_icons[index].selected = selected;
    }

    void close_app_window(size_t index)
    {
        g_app_windows[index].title = nullptr;
        g_app_windows[index].command = nullptr;
        g_app_windows[index].column = 0;
        g_app_windows[index].row = 0;
        g_app_windows[index].width = 0;
        g_app_windows[index].height = 0;
        g_app_windows[index].open = false;
        g_app_windows[index].focused = false;
    }

    bool icon_hit_test(const tinyos::ui::desktop::DesktopIcon& icon, uint32_t column, uint32_t row)
    {
        return column >= icon.column &&
            column < icon.column + IconWidth &&
            row >= icon.row &&
            row < icon.row + IconHeight;
    }

    bool select_icon_at(uint32_t column, uint32_t row)
    {
        for (size_t index = 0; index < g_state.icon_count; ++index)
        {
            if (icon_hit_test(g_icons[index], column, row))
            {
                g_icons[g_state.selected_index].selected = false;
                g_state.selected_index = index;
                g_icons[g_state.selected_index].selected = true;
                ++g_selection_changes;
                return true;
            }
        }

        return false;
    }

    const tinyos::ui::window_manager::Window* shell_window()
    {
        for (size_t index = 0; index < tinyos::ui::window_manager::window_count(); ++index)
        {
            const auto* window = tinyos::ui::window_manager::window_at(index);
            if (window != nullptr && window->role == tinyos::ui::window_manager::WindowRole::Shell)
            {
                return window;
            }
        }

        return nullptr;
    }

    bool draw_desktop_icon(const tinyos::ui::desktop::DesktopIcon& icon)
    {
        if (icon.title == nullptr || icon.command == nullptr)
        {
            return false;
        }

        const uint8_t attribute = icon.selected ? SelectedIconAttribute : IconAttribute;
        bool ok = tinyos::ui::renderer::fill_rect(icon.column, icon.row, IconWidth, IconHeight, ' ', attribute);
        ok = tinyos::ui::renderer::draw_text(icon.column + 5, icon.row, "[]", attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(icon.column + 1, icon.row + 1, icon.title, attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(icon.column + 1, icon.row + 2, icon.command, attribute) && ok;
        return ok;
    }

    bool draw_top_panel()
    {
        bool ok = tinyos::ui::renderer::fill_rect(0, 0, g_state.columns, 1, ' ', TopPanelAttribute);
        ok = tinyos::ui::renderer::draw_text(1, 0, "TinyOS", TopPanelAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(12, 0, "1  2  3  4", TopPanelAttribute) && ok;
        if (g_state.columns > 68)
        {
            ok = tinyos::ui::renderer::draw_text(55, 0, "desktop mode", TopPanelAttribute) && ok;
        }
        if (g_state.columns > 8)
        {
            ok = tinyos::ui::renderer::draw_text(g_state.columns - 8, 0, "11:45", TopPanelAttribute) && ok;
        }
        return ok;
    }

    bool draw_desktop_background()
    {
        bool ok = tinyos::ui::renderer::fill_rect(0, 0, g_state.columns, g_state.rows, ' ', WallpaperAttribute);
        ok = draw_top_panel() && ok;
        ok = tinyos::ui::renderer::draw_text(28, 8, "TinyOS", WallpaperAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(24, 10, "low-resource desktop", WallpaperAttribute) && ok;
        return ok;
    }

    bool draw_app_window(const tinyos::ui::desktop::AppWindow& window)
    {
        if (!window.open || window.title == nullptr || window.command == nullptr || window.width < 16 || window.height < 5)
        {
            return false;
        }

        const uint8_t attribute = window.focused ? FocusedAppWindowAttribute : AppWindowAttribute;
        bool ok = tinyos::ui::renderer::fill_rect(window.column, window.row, window.width, 1, ' ', attribute);
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row + window.height - 1, window.width, 1, '-', attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row, 1, window.height, '|', attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column + window.width - 1, window.row, 1, window.height, '|', attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column + 1, window.row + 1, window.width - 2, window.height - 2, ' ', attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 2, window.row, window.title, attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + window.width - 6, window.row, "[_][x]", attribute) && ok;
        ok = tinyos::ui::renderer::fill_rect(window.column + 1, window.row + 1, window.width - 2, 1, ' ', MenuAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 2, window.row + 1, "File  Actions  View  Help", MenuAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 3, window.row + 3, "Application window", attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 3, window.row + 4, window.command, attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 3, window.row + window.height - 2, "Tab: select  Enter: open  q: shell", attribute) && ok;
        return ok;
    }

    bool open_app_window_for_selected()
    {
        const auto* icon = tinyos::ui::desktop::icon_at(g_state.selected_index);
        if (icon == nullptr || icon->title == nullptr || icon->command == nullptr)
        {
            return false;
        }

        size_t slot = MaxAppWindows;
        for (size_t index = 0; index < MaxAppWindows; ++index)
        {
            if (g_app_windows[index].open && g_app_windows[index].command == icon->command)
            {
                slot = index;
                break;
            }

            if (!g_app_windows[index].open && slot == MaxAppWindows)
            {
                slot = index;
            }
        }

        if (slot == MaxAppWindows)
        {
            slot = 0;
        }

        for (size_t index = 0; index < MaxAppWindows; ++index)
        {
            g_app_windows[index].focused = false;
        }

        const uint32_t offset = static_cast<uint32_t>(slot * 2);
        g_app_windows[slot].title = icon->title;
        g_app_windows[slot].command = icon->command;
        g_app_windows[slot].column = 18 + offset;
        g_app_windows[slot].row = 4 + offset;
        g_app_windows[slot].width = g_state.columns > 58 ? 56 : 34;
        g_app_windows[slot].height = g_state.rows > 20 ? 15 : 8;
        g_app_windows[slot].open = true;
        g_app_windows[slot].focused = true;
        return true;
    }

    bool render_workspace(bool fullscreen)
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

        bool ok = false;
        uint32_t status_column = 0;
        uint32_t status_row = 0;

        g_state.fullscreen = fullscreen;
        if (fullscreen)
        {
            ok = draw_desktop_background();
            status_column = 1;
            status_row = g_state.rows > 0 ? g_state.rows - 1 : 0;
        }
        else
        {
            if (!tinyos::ui::window_manager::compose())
            {
                ++g_rejected_operations;
                return false;
            }

            const auto* shell = shell_window();
            if (shell == nullptr || shell->width < 40 || shell->height < 10)
            {
                ++g_rejected_operations;
                return false;
            }

            ok = true;
            status_column = shell->column + 3;
            status_row = shell->row + shell->height - 2;
        }

        for (size_t index = 0; index < g_state.icon_count; ++index)
        {
            ok = draw_desktop_icon(g_icons[index]) && ok;
        }
        for (size_t index = 0; index < g_state.app_window_count; ++index)
        {
            if (g_app_windows[index].open)
            {
                ok = draw_app_window(g_app_windows[index]) && ok;
            }
        }

        ok = tinyos::ui::renderer::fill_rect(status_column, status_row, g_state.columns > status_column ? g_state.columns - status_column : 1, 1, ' ', StatusAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(status_column, status_row, "Tab/n: select  Enter/Space: open  q: shell", StatusAttribute) && ok;
        if (tinyos::ui::cursor::is_ready())
        {
            ok = tinyos::ui::cursor::render() && ok;
        }

        if (!ok)
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_renders;
        return true;
    }
}

namespace tinyos::ui::desktop
{
    void initialize()
    {
        const auto* renderer_state = tinyos::ui::renderer::state();
        if (renderer_state == nullptr || !renderer_state->ready || !tinyos::ui::window_manager::is_ready())
        {
            g_state.ready = false;
            g_state.columns = 0;
            g_state.rows = 0;
            g_state.item_count = 0;
            g_state.selected_index = 0;
            g_state.icon_count = 0;
            g_state.app_window_count = 0;
            g_state.pointer_column = 0;
            g_state.pointer_row = 0;
            g_state.fullscreen = false;
            return;
        }

        g_state.ready = true;
        g_state.columns = renderer_state->width;
        g_state.rows = renderer_state->height;
        g_state.item_count = MaxLauncherItems;
        g_state.selected_index = 0;
        g_state.icon_count = MaxLauncherItems;
        g_state.app_window_count = MaxAppWindows;
        g_state.pointer_column = 0;
        g_state.pointer_row = 0;
        g_state.fullscreen = false;

        const uint32_t icon_column = FullscreenIconColumn;
        const uint32_t icon_row = FullscreenIconFirstRow;

        set_launcher_item(0, "Terminal", "wmtest", true);
        set_launcher_item(1, "Devices", "devices", true);
        set_launcher_item(2, "Security", "securityinfo", true);
        set_launcher_item(3, "Settings", "desktopinfo", true);
        set_icon(0, "Terminal", "wmtest", icon_column, icon_row, true);
        set_icon(1, "Devices", "devices", icon_column, icon_row + 3, false);
        set_icon(2, "Security", "securityinfo", icon_column, icon_row + 6, false);
        set_icon(3, "Settings", "desktopinfo", icon_column, icon_row + 9, false);
        for (size_t index = 0; index < MaxAppWindows; ++index)
        {
            close_app_window(index);
        }
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    size_t launcher_item_count()
    {
        return g_state.item_count;
    }

    const LauncherItem* launcher_item_at(size_t index)
    {
        if (index >= g_state.item_count)
        {
            return nullptr;
        }

        return &g_items[index];
    }

    const LauncherItem* selected_launcher_item()
    {
        return launcher_item_at(g_state.selected_index);
    }

    size_t icon_count()
    {
        return g_state.icon_count;
    }

    const DesktopIcon* icon_at(size_t index)
    {
        if (index >= g_state.icon_count)
        {
            return nullptr;
        }

        return &g_icons[index];
    }

    size_t app_window_count()
    {
        return g_state.app_window_count;
    }

    const AppWindow* app_window_at(size_t index)
    {
        if (index >= g_state.app_window_count)
        {
            return nullptr;
        }

        return &g_app_windows[index];
    }

    size_t open_app_window_count()
    {
        size_t open_count = 0;
        for (size_t index = 0; index < g_state.app_window_count; ++index)
        {
            if (g_app_windows[index].open)
            {
                ++open_count;
            }
        }

        return open_count;
    }

    bool select_next()
    {
        if (!g_state.ready || g_state.icon_count == 0)
        {
            ++g_rejected_operations;
            return false;
        }

        g_icons[g_state.selected_index].selected = false;
        g_state.selected_index = (g_state.selected_index + 1) % g_state.icon_count;
        g_icons[g_state.selected_index].selected = true;
        ++g_selection_changes;
        return true;
    }

    bool select_previous()
    {
        if (!g_state.ready || g_state.icon_count == 0)
        {
            ++g_rejected_operations;
            return false;
        }

        g_icons[g_state.selected_index].selected = false;
        g_state.selected_index = g_state.selected_index == 0 ? g_state.icon_count - 1 : g_state.selected_index - 1;
        g_icons[g_state.selected_index].selected = true;
        ++g_selection_changes;
        return true;
    }

    bool render_home()
    {
        return render_workspace(false);
    }

    bool render_fullscreen()
    {
        return render_workspace(true);
    }

    bool launch_selected()
    {
        const auto* item = selected_launcher_item();
        if (item == nullptr || !item->enabled || item->command == nullptr)
        {
            ++g_rejected_operations;
            return false;
        }

        if (!open_app_window_for_selected())
        {
            ++g_rejected_operations;
            return false;
        }

        if (g_state.fullscreen)
        {
            if (!render_fullscreen())
            {
                ++g_rejected_operations;
                return false;
            }
        }
        else if (!render_home())
        {
            ++g_rejected_operations;
            return false;
        }

        const uint32_t column = g_state.fullscreen ? 1 : 6;
        const uint32_t row = g_state.rows > 1 ? g_state.rows - 2 : 0;
        bool ok = tinyos::ui::renderer::fill_rect(column, row, g_state.columns > column ? g_state.columns - column : 1, 1, ' ', StatusAttribute);
        ok = tinyos::ui::renderer::draw_text(column, row, "Opened:", StatusAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(column + 9, row, item->title, StatusAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(column + 22, row, item->command, StatusAttribute) && ok;
        if (!ok)
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_launch_requests;
        return true;
    }

    bool render_demo()
    {
        return render_fullscreen();
    }

    bool handle_event(const tinyos::ui::events::Event& event)
    {
        if (!g_state.ready || event.type == tinyos::ui::events::EventType::None)
        {
            ++g_rejected_operations;
            return false;
        }

        if (event.type == tinyos::ui::events::EventType::Key && event.pressed)
        {
            ++g_handled_events;
            if (event.character == '\t' || event.character == 'n' || event.character == 's' || event.character == 'j')
            {
                return select_next() && (g_state.fullscreen ? render_fullscreen() : render_home());
            }

            if (event.character == 'p' || event.character == 'w' || event.character == 'k')
            {
                return select_previous() && (g_state.fullscreen ? render_fullscreen() : render_home());
            }

            if (event.character == '\n' || event.character == ' ')
            {
                return launch_selected();
            }

            return g_state.fullscreen ? render_fullscreen() : render_home();
        }

        if (event.type == tinyos::ui::events::EventType::Pointer)
        {
            g_state.pointer_column = event.column;
            g_state.pointer_row = event.row;
            tinyos::ui::cursor::handle_event(event);
            ++g_pointer_events;
            ++g_handled_events;
            return g_state.fullscreen ? render_fullscreen() : render_home();
        }

        if (event.type == tinyos::ui::events::EventType::MouseButton && event.pressed)
        {
            g_state.pointer_column = event.column;
            g_state.pointer_row = event.row;
            tinyos::ui::cursor::handle_event(event);
            ++g_pointer_events;
            ++g_handled_events;
            if (!select_icon_at(event.column, event.row))
            {
                return g_state.fullscreen ? render_fullscreen() : render_home();
            }

            return launch_selected();
        }

        ++g_rejected_operations;
        return false;
    }

    size_t dispatch_events(size_t max_events)
    {
        size_t dispatched = 0;
        while (dispatched < max_events)
        {
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

            if (!tinyos::ui::events::poll_event(event))
            {
                break;
            }

            if (handle_event(event))
            {
                ++dispatched;
            }
        }

        return dispatched;
    }

    uint64_t render_count()
    {
        return g_renders;
    }

    uint64_t selection_change_count()
    {
        return g_selection_changes;
    }

    uint64_t launch_request_count()
    {
        return g_launch_requests;
    }

    uint64_t handled_event_count()
    {
        return g_handled_events;
    }

    uint64_t pointer_event_count()
    {
        return g_pointer_events;
    }

    uint64_t rejected_operation_count()
    {
        return g_rejected_operations;
    }

    bool validation_self_test()
    {
        return g_state.ready &&
            tinyos::ui::window_manager::validation_self_test() &&
            g_state.columns >= 40 &&
            g_state.rows >= 15 &&
            g_state.item_count == MaxLauncherItems &&
            g_state.icon_count == MaxLauncherItems &&
            g_state.app_window_count == MaxAppWindows &&
            g_state.selected_index < g_state.icon_count;
    }

    bool launcher_validation_self_test()
    {
        const auto* item = selected_launcher_item();
        return validation_self_test() &&
            item != nullptr &&
            item->enabled &&
            item->title != nullptr &&
            item->command != nullptr &&
            render_home();
    }

    bool interaction_validation_self_test()
    {
        if (!launcher_validation_self_test())
        {
            return false;
        }

        const size_t original_index = g_state.selected_index;
        const bool selected = select_next();
        const bool drawn = selected && render_home();
        const bool launched = drawn && launch_selected();
        g_state.selected_index = original_index;
        for (size_t index = 0; index < g_state.icon_count; ++index)
        {
            g_icons[index].selected = index == original_index;
        }
        for (size_t index = 0; index < g_state.app_window_count; ++index)
        {
            close_app_window(index);
        }
        const bool restored = launched && render_home();
        return selected && drawn && launched && restored;
    }

    bool fullscreen_validation_self_test()
    {
        if (!validation_self_test())
        {
            return false;
        }

        const size_t original_index = g_state.selected_index;
        const bool rendered = render_fullscreen();
        const bool selected = rendered && select_next();
        const bool navigated = selected && render_fullscreen();
        const bool launched = navigated && launch_selected();
        g_state.selected_index = original_index;
        for (size_t index = 0; index < g_state.icon_count; ++index)
        {
            g_icons[index].selected = index == original_index;
        }
        for (size_t index = 0; index < g_state.app_window_count; ++index)
        {
            close_app_window(index);
        }
        g_state.fullscreen = false;
        const bool restored = render_home();
        return rendered && selected && navigated && launched && restored;
    }

    bool input_validation_self_test()
    {
        if (!validation_self_test() || !tinyos::ui::events::is_ready())
        {
            return false;
        }

        const size_t original_index = g_state.selected_index;
        const auto* icon = icon_at(1);
        if (icon == nullptr)
        {
            return false;
        }

        const bool key_queued = tinyos::ui::events::push_key_event('\t', true);
        const size_t key_dispatched = dispatch_events(1);
        const bool mouse_queued = tinyos::ui::events::push_mouse_button_event(icon->column + 1, icon->row + 1, 1, true);
        const size_t mouse_dispatched = dispatch_events(1);

        g_state.selected_index = original_index;
        for (size_t index = 0; index < g_state.icon_count; ++index)
        {
            g_icons[index].selected = index == original_index;
        }
        for (size_t index = 0; index < g_state.app_window_count; ++index)
        {
            close_app_window(index);
        }
        const bool restored = render_home();

        return key_queued &&
            key_dispatched == 1 &&
            mouse_queued &&
            mouse_dispatched == 1 &&
            restored;
    }
}