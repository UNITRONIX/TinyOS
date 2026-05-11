#include <tinyos/ui/desktop.hpp>
#include <tinyos/ui/cursor.hpp>
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/window_manager.hpp>

namespace
{
    constexpr size_t MaxLauncherItems = 4;
    constexpr size_t MaxAppWindows = 3;
    // Ubuntu/Unity-inspired aubergine-red theme. Attribute = (bg<<4)|fg, VGA palette.
    constexpr uint8_t WallpaperAttribute = 0x44;            // solid red wallpaper
    constexpr uint8_t WallpaperAccentAttribute = 0x4C;      // red bg, light red highlights
    constexpr uint8_t TopPanelAttribute = 0x4F;             // red bg, white fg
    constexpr uint8_t TopPanelSubAttribute = 0x4E;          // red bg, yellow accent
    constexpr uint8_t SidebarAttribute = 0x40;              // red bg, black fg
    constexpr uint8_t SidebarIconAttribute = 0x4F;          // red bg, white fg
    constexpr uint8_t SidebarSelectedAttribute = 0x6F;      // brown/orange bg, white fg
    constexpr uint8_t SearchBarAttribute = 0x70;            // gray card bg, black fg
    constexpr uint8_t SearchBarHintAttribute = 0x78;        // gray bg, dim fg
    constexpr uint8_t SectionHeaderAttribute = 0x4F;        // red bg, white fg
    constexpr uint8_t SectionSubAttribute = 0x4E;           // red bg, yellow link
    constexpr uint8_t IconAttribute = 0x70;                 // tile card: gray bg, black fg
    constexpr uint8_t IconGlyphAttribute = 0x74;            // tile glyph: gray bg, red fg
    constexpr uint8_t IconLabelAttribute = 0x4F;            // label below tile on wallpaper
    constexpr uint8_t SelectedIconAttribute = 0x60;         // orange bg, black fg
    constexpr uint8_t SelectedIconGlyphAttribute = 0x64;    // orange bg, red fg
    constexpr uint8_t SelectedIconLabelAttribute = 0x6F;    // orange bg, white fg
    constexpr uint8_t FileTileAttribute = 0x70;             // gray bg, black fg
    constexpr uint8_t FileTileGlyphAttribute = 0x71;        // gray bg, blue fg
    constexpr uint8_t AppWindowAttribute = 0x4F;            // window title bar
    constexpr uint8_t FocusedAppWindowAttribute = 0x4F;     // focused window title bar (same red)
    constexpr uint8_t AppWindowBodyAttribute = 0x0F;        // black bg, white fg (terminal body)
    constexpr uint8_t AppWindowPromptAttribute = 0x0A;      // black bg, green fg (prompt)
    constexpr uint8_t AppWindowDimAttribute = 0x08;         // black bg, dim gray fg
    constexpr uint8_t MenuAttribute = 0x70;
    constexpr uint8_t StatusAttribute = 0x70;
    constexpr uint8_t HintAttribute = 0x40;                 // red bg, black fg footer
    constexpr uint32_t SidebarWidth = 5;
    constexpr uint32_t IconWidth = 14;
    constexpr uint32_t IconHeight = 3;
    constexpr uint32_t FullscreenIconColumn = 8;
    constexpr uint32_t FullscreenIconFirstRow = 7;
    constexpr uint32_t FullscreenIconStep = 16;             // 14 wide + 2 gap

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

        const bool selected = icon.selected;
        const uint8_t tile_attr = selected ? SelectedIconAttribute : IconAttribute;
        const uint8_t glyph_attr = selected ? SelectedIconGlyphAttribute : IconGlyphAttribute;
        const uint8_t label_attr = selected ? SelectedIconLabelAttribute : IconLabelAttribute;

        // 14x3 card tile that looks like a launcher icon.
        bool ok = tinyos::ui::renderer::fill_rect(icon.column, icon.row, IconWidth, IconHeight, ' ', tile_attr);
        ok = tinyos::ui::renderer::draw_text(icon.column + 1, icon.row, ".------------.", tile_attr) && ok;
        ok = tinyos::ui::renderer::draw_text(icon.column + 1, icon.row + 1, "|            |", tile_attr) && ok;
        ok = tinyos::ui::renderer::draw_text(icon.column + 1, icon.row + 2, "`------------'", tile_attr) && ok;
        // Pick a short glyph based on first character of title.
        const char first = icon.title[0];
        const char* glyph = "[ ]";
        if (first == 'T' || first == 't') glyph = ">_ ";
        else if (first == 'D' || first == 'd') glyph = "(o)";
        else if (first == 'S' || first == 's') glyph = "<#>";
        else if (first == 'F' || first == 'f') glyph = "[D]";
        else if (first == 'B' || first == 'b') glyph = "(W)";
        else if (first == 'E' || first == 'e') glyph = "/ \\";
        ok = tinyos::ui::renderer::draw_text(icon.column + 6, icon.row + 1, glyph, glyph_attr) && ok;
        // Label below the tile on the wallpaper.
        if (icon.row + IconHeight < g_state.rows)
        {
            const uint32_t label_width = IconWidth;
            ok = tinyos::ui::renderer::fill_rect(icon.column, icon.row + IconHeight, label_width, 1, ' ', label_attr) && ok;
            ok = tinyos::ui::renderer::draw_text(icon.column + 2, icon.row + IconHeight, icon.title, label_attr) && ok;
        }
        return ok;
    }

    bool draw_top_panel()
    {
        bool ok = tinyos::ui::renderer::fill_rect(0, 0, g_state.columns, 1, ' ', TopPanelAttribute);
        // Left cluster: window controls + brand.
        ok = tinyos::ui::renderer::draw_text(1, 0, "o x -", TopPanelSubAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(8, 0, "TinyOS", TopPanelAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(16, 0, "Activities", TopPanelAttribute) && ok;
        if (g_state.columns > 50)
        {
            ok = tinyos::ui::renderer::draw_text(30, 0, "Files  Edit  View  Help", TopPanelAttribute) && ok;
        }
        if (g_state.columns > 8)
        {
            ok = tinyos::ui::renderer::draw_text(g_state.columns - 7, 0, "11:45", TopPanelAttribute) && ok;
        }
        return ok;
    }

    bool draw_sidebar()
    {
        if (g_state.columns < SidebarWidth + 1 || g_state.rows < 3)
        {
            return true;
        }

        const uint32_t start_row = 1;
        const uint32_t end_row = g_state.rows > 1 ? g_state.rows - 1 : g_state.rows;
        bool ok = tinyos::ui::renderer::fill_rect(0, start_row, SidebarWidth, end_row - start_row, ' ', SidebarAttribute);

        struct DockEntry { const char* glyph; bool highlight; };
        const DockEntry entries[] = {
            { "(O)", true },   // home/Ubuntu mark
            { "[D]", false },  // files
            { "(F)", false },  // browser
            { "[T]", false },  // text editor
            { "[X]", false },  // spreadsheet
            { ">_ ", false },  // terminal
            { "<#>", false },  // settings
            { "[?]", false },  // help
        };
        const uint32_t entry_height = 2;
        const uint32_t available = end_row > start_row + 1 ? end_row - start_row - 1 : 0;
        const uint32_t count = sizeof(entries) / sizeof(entries[0]);
        const uint32_t max_entries = available / entry_height;
        const uint32_t shown = count < max_entries ? count : max_entries;
        for (uint32_t i = 0; i < shown; ++i)
        {
            const uint32_t row = start_row + 1 + i * entry_height;
            const uint8_t attr = entries[i].highlight ? SidebarSelectedAttribute : SidebarIconAttribute;
            ok = tinyos::ui::renderer::fill_rect(0, row, SidebarWidth, 1, ' ', attr) && ok;
            ok = tinyos::ui::renderer::draw_text(1, row, entries[i].glyph, attr) && ok;
        }
        return ok;
    }

    bool draw_search_bar()
    {
        if (g_state.columns < SidebarWidth + 12 || g_state.rows < 4)
        {
            return true;
        }
        const uint32_t col = SidebarWidth + 1;
        const uint32_t row = 2;
        const uint32_t width = g_state.columns - col - 1;
        bool ok = tinyos::ui::renderer::fill_rect(col, row, width, 1, ' ', SearchBarAttribute);
        ok = tinyos::ui::renderer::draw_text(col + 1, row, "Q", SearchBarAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(col + 3, row, "Search", SearchBarHintAttribute) && ok;
        return ok;
    }

    bool draw_sections()
    {
        if (g_state.columns < SidebarWidth + 20 || g_state.rows < 12)
        {
            return true;
        }
        const uint32_t col = SidebarWidth + 1;
        bool ok = tinyos::ui::renderer::draw_text(col, 4, "|||  Recent Apps", SectionHeaderAttribute);
        ok = tinyos::ui::renderer::draw_text(col + 18, 4, "see 5 more results >", SectionSubAttribute) && ok;

        const uint32_t files_row = g_state.rows > 15 ? 12 : 11;
        ok = tinyos::ui::renderer::draw_text(col, files_row, "|||  Recent Files", SectionHeaderAttribute) && ok;

        // Decorative recent-file tiles.
        if (g_state.rows > files_row + 4)
        {
            struct FileTile { const char* glyph; const char* label; };
            const FileTile tiles[] = {
                { "(.)", "boot.log" },
                { "[M]", "notes.md" },
                { "[T]", "report.odt" },
            };
            const uint32_t base = files_row + 2;
            for (uint32_t i = 0; i < 3; ++i)
            {
                const uint32_t tcol = col + 1 + i * 16;
                if (tcol + IconWidth > g_state.columns) break;
                ok = tinyos::ui::renderer::fill_rect(tcol, base, IconWidth, IconHeight, ' ', FileTileAttribute) && ok;
                ok = tinyos::ui::renderer::draw_text(tcol + 1, base, ".------------.", FileTileAttribute) && ok;
                ok = tinyos::ui::renderer::draw_text(tcol + 1, base + 1, "|            |", FileTileAttribute) && ok;
                ok = tinyos::ui::renderer::draw_text(tcol + 1, base + 2, "`------------'", FileTileAttribute) && ok;
                ok = tinyos::ui::renderer::draw_text(tcol + 6, base + 1, tiles[i].glyph, FileTileGlyphAttribute) && ok;
                if (base + IconHeight < g_state.rows)
                {
                    ok = tinyos::ui::renderer::draw_text(tcol + 2, base + IconHeight, tiles[i].label, IconLabelAttribute) && ok;
                }
            }
        }
        return ok;
    }

    bool draw_desktop_background()
    {
        bool ok = tinyos::ui::renderer::fill_rect(0, 0, g_state.columns, g_state.rows, ' ', WallpaperAttribute);
        // Subtle wallpaper texture using accent attribute on a few rows.
        for (uint32_t r = 1; r < g_state.rows; r += 3)
        {
            ok = tinyos::ui::renderer::draw_text(SidebarWidth + 1, r, " ", WallpaperAccentAttribute) && ok;
        }
        ok = draw_top_panel() && ok;
        ok = draw_sidebar() && ok;
        ok = draw_search_bar() && ok;
        ok = draw_sections() && ok;
        return ok;
    }

    bool draw_app_window(const tinyos::ui::desktop::AppWindow& window)
    {
        if (!window.open || window.title == nullptr || window.command == nullptr || window.width < 16 || window.height < 5)
        {
            return false;
        }

        const uint8_t title_attr = window.focused ? FocusedAppWindowAttribute : AppWindowAttribute;
        // Title bar
        bool ok = tinyos::ui::renderer::fill_rect(window.column, window.row, window.width, 1, ' ', title_attr);
        ok = tinyos::ui::renderer::draw_text(window.column + 1, window.row, "o x -", title_attr) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 8, window.row, window.title, title_attr) && ok;
        if (window.width > 10)
        {
            ok = tinyos::ui::renderer::draw_text(window.column + window.width - 9, window.row, "[_][O][x]", title_attr) && ok;
        }
        // Menu strip
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row + 1, window.width, 1, ' ', MenuAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 1, window.row + 1, "File  Edit  View  Terminal  Help", MenuAttribute) && ok;
        // Body (terminal-style)
        ok = tinyos::ui::renderer::fill_rect(window.column, window.row + 2, window.width, window.height - 3, ' ', AppWindowBodyAttribute) && ok;
        const uint32_t body_col = window.column + 2;
        uint32_t line = window.row + 3;
        if (line < window.row + window.height - 2)
        {
            ok = tinyos::ui::renderer::draw_text(body_col, line, "tinyos@host:~$ ", AppWindowPromptAttribute) && ok;
            ok = tinyos::ui::renderer::draw_text(body_col + 15, line, window.command, AppWindowBodyAttribute) && ok;
            ++line;
        }
        if (line < window.row + window.height - 2)
        {
            ok = tinyos::ui::renderer::draw_text(body_col, line, " apps  build  docs  modules  README.md", AppWindowDimAttribute) && ok;
            ++line;
        }
        if (line < window.row + window.height - 2)
        {
            ok = tinyos::ui::renderer::draw_text(body_col, line, "tinyos@host:~$ uname -a", AppWindowPromptAttribute) && ok;
            ++line;
        }
        if (line < window.row + window.height - 2)
        {
            ok = tinyos::ui::renderer::draw_text(body_col, line, " TinyOS 0.1 i686 desktop modular kernel", AppWindowDimAttribute) && ok;
            ++line;
        }
        if (line < window.row + window.height - 2)
        {
            ok = tinyos::ui::renderer::draw_text(body_col, line, "tinyos@host:~$ _", AppWindowPromptAttribute) && ok;
        }
        // Footer status bar inside window
        const uint32_t footer = window.row + window.height - 1;
        ok = tinyos::ui::renderer::fill_rect(window.column, footer, window.width, 1, ' ', StatusAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(window.column + 1, footer, "Tab: select  Enter: open  q: shell", StatusAttribute) && ok;
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
        if (g_state.columns >= 72)
        {
            set_icon(0, "Terminal", "wmtest", icon_column + 0 * FullscreenIconStep, icon_row, true);
            set_icon(1, "Devices", "devices", icon_column + 1 * FullscreenIconStep, icon_row, false);
            set_icon(2, "Security", "securityinfo", icon_column + 2 * FullscreenIconStep, icon_row, false);
            set_icon(3, "Settings", "desktopinfo", icon_column + 3 * FullscreenIconStep, icon_row, false);
        }
        else
        {
            set_icon(0, "Terminal", "wmtest", icon_column, icon_row, true);
            set_icon(1, "Devices", "devices", icon_column, icon_row + 4, false);
            set_icon(2, "Security", "securityinfo", icon_column, icon_row + 8, false);
            set_icon(3, "Settings", "desktopinfo", icon_column, icon_row + 12, false);
        }
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