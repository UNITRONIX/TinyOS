#include <stdint.h>

#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>

namespace
{
    constexpr uint8_t StatusAttribute = 0x1F;
    constexpr uint8_t SelfTestAttribute = 0x0A;
    constexpr uint8_t ContentAttribute = 0x07;
    constexpr uint8_t PanelAttribute = 0x17;

    tinyos::ui::terminal::State g_state = {};
    uint64_t g_status_updates = 0;
    uint64_t g_line_writes = 0;
    uint64_t g_clear_operations = 0;
    uint64_t g_panel_draws = 0;
    uint64_t g_rejected_operations = 0;

    bool text_ready()
    {
        return g_state.ready &&
            g_state.backend == tinyos::ui::renderer::Backend::TextGrid &&
            g_state.columns != 0 &&
            g_state.rows > 1 &&
            g_state.content_rows != 0;
    }
}

namespace tinyos::ui::terminal
{
    void initialize()
    {
        const auto* renderer_state = tinyos::ui::renderer::state();
        if (renderer_state == nullptr || !renderer_state->ready || !renderer_state->text_output)
        {
            g_state.ready = false;
            g_state.backend = tinyos::ui::renderer::Backend::None;
            g_state.columns = 0;
            g_state.rows = 0;
            g_state.status_row = 0;
            g_state.content_first_row = 0;
            g_state.content_rows = 0;
            return;
        }

        g_state.backend = renderer_state->backend;
        g_state.columns = renderer_state->width;
        g_state.rows = renderer_state->height;
        g_state.status_row = 0;
        g_state.content_first_row = 1;
        g_state.content_rows = renderer_state->height > 1 ? renderer_state->height - 1 : 0;
        g_state.ready = renderer_state->backend == tinyos::ui::renderer::Backend::TextGrid && g_state.content_rows != 0;
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    bool draw_status(const char* text)
    {
        if (text == nullptr || !text_ready())
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::draw_text(0, g_state.status_row, text, StatusAttribute))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_status_updates;
        return true;
    }

    bool clear_status()
    {
        if (!text_ready())
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::clear_area(0, g_state.status_row, g_state.columns, 1, StatusAttribute))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_clear_operations;
        return true;
    }

    bool clear_content()
    {
        if (!text_ready())
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::clear_area(0, g_state.content_first_row, g_state.columns, g_state.content_rows, ContentAttribute))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_clear_operations;
        return true;
    }

    bool write_line(uint32_t row, const char* text, uint8_t attribute)
    {
        if (text == nullptr || !text_ready() || row >= g_state.content_rows)
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::draw_text(0, g_state.content_first_row + row, text, attribute))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_line_writes;
        return true;
    }

    bool draw_panel(uint32_t row, uint32_t height, const char* title)
    {
        if (title == nullptr || !text_ready() || height < 3 || height > g_state.content_rows || row > g_state.content_rows - height)
        {
            ++g_rejected_operations;
            return false;
        }

        const uint32_t top = g_state.content_first_row + row;
        const uint32_t bottom = top + height - 1;
        const uint32_t side_height = height - 2;

        const bool top_line = tinyos::ui::renderer::fill_rect(0, top, g_state.columns, 1, '-', PanelAttribute);
        const bool bottom_line = tinyos::ui::renderer::fill_rect(0, bottom, g_state.columns, 1, '-', PanelAttribute);
        const bool left_side = tinyos::ui::renderer::fill_rect(0, top + 1, 1, side_height, '|', PanelAttribute);
        const bool right_side = tinyos::ui::renderer::fill_rect(g_state.columns - 1, top + 1, 1, side_height, '|', PanelAttribute);
        const bool title_drawn = tinyos::ui::renderer::draw_text(2, top, title, PanelAttribute);

        if (!top_line || !bottom_line || !left_side || !right_side || !title_drawn)
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_panel_draws;
        return true;
    }

    bool render_self_test_label()
    {
        if (!draw_status("TinyOS terminal UI ready"))
        {
            return false;
        }

        return write_line(g_state.content_rows - 1, "Terminal renderer path OK", SelfTestAttribute);
    }

    bool render_panel_self_test()
    {
        if (!clear_content())
        {
            return false;
        }

        if (!draw_panel(1, 5, "TinyOS terminal panel"))
        {
            return false;
        }

        return write_line(3, "Panel path OK", SelfTestAttribute);
    }

    uint64_t status_update_count()
    {
        return g_status_updates;
    }

    uint64_t line_write_count()
    {
        return g_line_writes;
    }

    uint64_t clear_operation_count()
    {
        return g_clear_operations;
    }

    uint64_t panel_draw_count()
    {
        return g_panel_draws;
    }

    uint64_t rejected_operation_count()
    {
        return g_rejected_operations;
    }

    bool validation_self_test()
    {
        return tinyos::ui::renderer::validation_self_test() &&
            g_state.ready &&
            g_state.backend == tinyos::ui::renderer::Backend::TextGrid &&
            g_state.columns >= 80 &&
            g_state.rows >= 25 &&
            g_state.status_row == 0 &&
            g_state.content_first_row == 1 &&
            g_state.content_rows == g_state.rows - 1;
    }

    bool panel_validation_self_test()
    {
        return validation_self_test() &&
            g_state.columns >= 20 &&
            g_state.content_rows >= 6 &&
            tinyos::ui::renderer::primitive_validation_self_test();
    }
}