#include <stdint.h>

#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>

namespace
{
    constexpr uint8_t StatusAttribute = 0x1F;
    constexpr uint8_t SelfTestAttribute = 0x0A;
    constexpr uint8_t ContentAttribute = 0x07;
    constexpr uint8_t PanelAttribute = 0x17;
    constexpr uint8_t AccentAttribute = 0x0B;
    constexpr uint8_t SuccessAttribute = 0x0A;
    constexpr uint8_t WarningAttribute = 0x0E;
    constexpr uint8_t ErrorAttribute = 0x0C;
    constexpr uint8_t SelectedAttribute = 0x2F;
    constexpr uint8_t DimAttribute = 0x08;

    constexpr tinyos::ui::terminal::TextSegment ColorDemoOverview[] = {
        { "section ", tinyos::ui::terminal::Style::Accent },
        { "status", tinyos::ui::terminal::Style::Success },
        { " / ", tinyos::ui::terminal::Style::Dim },
        { "warning", tinyos::ui::terminal::Style::Warning },
        { " / ", tinyos::ui::terminal::Style::Dim },
        { "error", tinyos::ui::terminal::Style::Error }
    };
    constexpr tinyos::ui::terminal::TextSegment ColorDemoOptionOne[] = {
        { "> ", tinyos::ui::terminal::Style::Selected },
        { "Open HelpUI", tinyos::ui::terminal::Style::Selected },
        { " ready", tinyos::ui::terminal::Style::Success }
    };
    constexpr tinyos::ui::terminal::TextSegment ColorDemoOptionTwo[] = {
        { "  ", tinyos::ui::terminal::Style::Dim },
        { "Terminal theme", tinyos::ui::terminal::Style::Accent },
        { " planned", tinyos::ui::terminal::Style::Warning }
    };
    constexpr tinyos::ui::terminal::TextSegment ColorDemoOptionThree[] = {
        { "  ", tinyos::ui::terminal::Style::Dim },
        { "Diagnostics", tinyos::ui::terminal::Style::Normal },
        { " safe", tinyos::ui::terminal::Style::Success }
    };

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

    uint32_t segment_width(const char* text, uint32_t remaining_columns)
    {
        uint32_t width = 0;
        while (text[width] != '\0' && text[width] != '\n' && width < remaining_columns)
        {
            ++width;
        }

        return width;
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
        return draw_status(text, Style::Status);
    }

    bool draw_status(const char* text, Style style)
    {
        if (text == nullptr || !text_ready())
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::draw_text(0, g_state.status_row, text, attribute_for_style(style)))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_status_updates;
        return true;
    }

    uint8_t attribute_for_style(Style style)
    {
        switch (style)
        {
        case Style::Normal:
            return ContentAttribute;
        case Style::Status:
            return StatusAttribute;
        case Style::Accent:
            return AccentAttribute;
        case Style::Success:
            return SuccessAttribute;
        case Style::Warning:
            return WarningAttribute;
        case Style::Error:
            return ErrorAttribute;
        case Style::Selected:
            return SelectedAttribute;
        case Style::Panel:
            return PanelAttribute;
        case Style::Dim:
            return DimAttribute;
        }

        return ContentAttribute;
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

    bool write_line(uint32_t row, const char* text, Style style)
    {
        return write_line(row, text, attribute_for_style(style));
    }

    bool write_segments(uint32_t row, uint32_t column, const TextSegment* segments, uint32_t count)
    {
        if (segments == nullptr || count == 0 || !text_ready() || row >= g_state.content_rows || column >= g_state.columns)
        {
            ++g_rejected_operations;
            return false;
        }

        uint32_t current_column = column;
        const uint32_t absolute_row = g_state.content_first_row + row;
        for (uint32_t index = 0; index < count && current_column < g_state.columns; ++index)
        {
            if (segments[index].text == nullptr)
            {
                ++g_rejected_operations;
                return false;
            }

            if (!tinyos::ui::renderer::draw_text(current_column, absolute_row, segments[index].text, attribute_for_style(segments[index].style)))
            {
                ++g_rejected_operations;
                return false;
            }

            current_column += segment_width(segments[index].text, g_state.columns - current_column);
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

    bool render_color_demo()
    {
        if (!clear_status() || !clear_content())
        {
            return false;
        }

        if (!draw_status("TinyOS terminal styles", Style::Status))
        {
            return false;
        }

        if (!draw_panel(1, 8, "Styled terminal demo"))
        {
            return false;
        }

        if (!write_segments(3, 3, ColorDemoOverview, sizeof(ColorDemoOverview) / sizeof(ColorDemoOverview[0])))
        {
            return false;
        }

        if (!write_segments(5, 3, ColorDemoOptionOne, sizeof(ColorDemoOptionOne) / sizeof(ColorDemoOptionOne[0])))
        {
            return false;
        }

        if (!write_segments(6, 3, ColorDemoOptionTwo, sizeof(ColorDemoOptionTwo) / sizeof(ColorDemoOptionTwo[0])))
        {
            return false;
        }

        return write_segments(7, 3, ColorDemoOptionThree, sizeof(ColorDemoOptionThree) / sizeof(ColorDemoOptionThree[0]));
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

    bool style_validation_self_test()
    {
        return panel_validation_self_test() &&
            attribute_for_style(Style::Normal) == ContentAttribute &&
            attribute_for_style(Style::Status) == StatusAttribute &&
            attribute_for_style(Style::Accent) == AccentAttribute &&
            attribute_for_style(Style::Success) == SuccessAttribute &&
            attribute_for_style(Style::Warning) == WarningAttribute &&
            attribute_for_style(Style::Error) == ErrorAttribute &&
            attribute_for_style(Style::Selected) == SelectedAttribute &&
            attribute_for_style(Style::Panel) == PanelAttribute &&
            attribute_for_style(Style::Dim) == DimAttribute;
    }
}