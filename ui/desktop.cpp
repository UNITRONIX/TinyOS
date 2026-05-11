#include <tinyos/ui/desktop.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/window_manager.hpp>

namespace
{
    constexpr size_t MaxLauncherItems = 4;
    constexpr uint8_t HeaderAttribute = 0x1F;
    constexpr uint8_t LauncherAttribute = 0x17;
    constexpr uint8_t SelectedLauncherAttribute = 0x2F;
    constexpr uint8_t StatusAttribute = 0x70;

    tinyos::ui::desktop::State g_state = {};
    tinyos::ui::desktop::LauncherItem g_items[MaxLauncherItems] = {};
    uint64_t g_renders = 0;
    uint64_t g_selection_changes = 0;
    uint64_t g_launch_requests = 0;
    uint64_t g_rejected_operations = 0;

    void set_launcher_item(size_t index, const char* title, const char* command, bool enabled)
    {
        g_items[index].title = title;
        g_items[index].command = command;
        g_items[index].enabled = enabled;
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

    bool draw_launcher_item(uint32_t column, uint32_t row, size_t index)
    {
        const auto* item = tinyos::ui::desktop::launcher_item_at(index);
        if (item == nullptr || item->title == nullptr || item->command == nullptr)
        {
            return false;
        }

        const bool selected = index == g_state.selected_index;
        const uint8_t attribute = selected ? SelectedLauncherAttribute : LauncherAttribute;
        bool ok = tinyos::ui::renderer::fill_rect(column, row, 34, 1, ' ', attribute);
        ok = tinyos::ui::renderer::draw_text(column + 1, row, selected ? ">" : " ", attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(column + 3, row, item->title, attribute) && ok;
        ok = tinyos::ui::renderer::draw_text(column + 20, row, item->command, attribute) && ok;
        return ok;
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
            return;
        }

        g_state.ready = true;
        g_state.columns = renderer_state->width;
        g_state.rows = renderer_state->height;
        g_state.item_count = MaxLauncherItems;
        g_state.selected_index = 0;

        set_launcher_item(0, "Shell", "wmtest", true);
        set_launcher_item(1, "Devices", "devices", true);
        set_launcher_item(2, "Security", "securityinfo", true);
        set_launcher_item(3, "Desktop", "desktopinfo", true);
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

    bool select_next()
    {
        if (!g_state.ready || g_state.item_count == 0)
        {
            ++g_rejected_operations;
            return false;
        }

        g_state.selected_index = (g_state.selected_index + 1) % g_state.item_count;
        ++g_selection_changes;
        return true;
    }

    bool render_home()
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

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

        const uint32_t column = shell->column + 3;
        const uint32_t row = shell->row + 2;
        bool ok = tinyos::ui::renderer::draw_text(column, row, "TinyOS desktop shell", HeaderAttribute);
        ok = tinyos::ui::renderer::draw_text(column, row + 2, "Launcher", HeaderAttribute) && ok;
        for (size_t index = 0; index < g_state.item_count; ++index)
        {
            ok = draw_launcher_item(column, row + 4 + static_cast<uint32_t>(index), index) && ok;
        }
        ok = tinyos::ui::renderer::draw_text(column, shell->row + shell->height - 2, "desktoptest renders this workspace", StatusAttribute) && ok;

        if (!ok)
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_renders;
        return true;
    }

    bool launch_selected()
    {
        const auto* item = selected_launcher_item();
        if (item == nullptr || !item->enabled || item->command == nullptr)
        {
            ++g_rejected_operations;
            return false;
        }

        if (!render_home())
        {
            return false;
        }

        const auto* shell = shell_window();
        if (shell == nullptr)
        {
            ++g_rejected_operations;
            return false;
        }

        const uint32_t column = shell->column + 3;
        const uint32_t row = shell->row + shell->height - 3;
        bool ok = tinyos::ui::renderer::fill_rect(column, row, shell->width - 6, 1, ' ', StatusAttribute);
        ok = tinyos::ui::renderer::draw_text(column, row, "Launch request:", StatusAttribute) && ok;
        ok = tinyos::ui::renderer::draw_text(column + 16, row, item->command, StatusAttribute) && ok;
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
        return render_home();
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
            g_state.item_count == MaxLauncherItems;
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
        const bool restored = launched && render_home();
        return selected && drawn && launched && restored;
    }
}