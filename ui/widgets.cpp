#include <stdint.h>

#include <tinyos/ui/events.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>
#include <tinyos/ui/widgets.hpp>

namespace
{
    constexpr uint8_t LabelAttribute = 0x0F;
    constexpr uint8_t ButtonAttribute = 0x70;
    constexpr uint8_t FocusedButtonAttribute = 0x2F;
    constexpr uint32_t ButtonWidth = 18;

    tinyos::ui::widgets::State g_state = {};
    uint64_t g_label_draws = 0;
    uint64_t g_button_draws = 0;
    uint64_t g_handled_events = 0;
    uint64_t g_activations = 0;
    uint64_t g_rejected_draws = 0;

    bool can_draw_content_row(uint32_t row)
    {
        return g_state.ready && row < g_state.content_rows;
    }

    uint32_t absolute_row(uint32_t row)
    {
        const auto* terminal_state = tinyos::ui::terminal::state();
        return terminal_state != nullptr ? terminal_state->content_first_row + row : row;
    }
}

namespace tinyos::ui::widgets
{
    void initialize()
    {
        const auto* terminal_state = tinyos::ui::terminal::state();
        if (terminal_state == nullptr || !terminal_state->ready)
        {
            g_state.ready = false;
            g_state.columns = 0;
            g_state.rows = 0;
            g_state.content_rows = 0;
            return;
        }

        g_state.ready = true;
        g_state.columns = terminal_state->columns;
        g_state.rows = terminal_state->rows;
        g_state.content_rows = terminal_state->content_rows;
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    bool draw_label(uint32_t row, const char* text)
    {
        if (text == nullptr || !can_draw_content_row(row))
        {
            ++g_rejected_draws;
            return false;
        }

        if (!tinyos::ui::terminal::write_line(row, text, LabelAttribute))
        {
            ++g_rejected_draws;
            return false;
        }

        ++g_label_draws;
        return true;
    }

    bool draw_button(uint32_t row, const char* text, bool focused)
    {
        if (text == nullptr || !can_draw_content_row(row))
        {
            ++g_rejected_draws;
            return false;
        }

        const uint32_t draw_row = absolute_row(row);
        const uint8_t attribute = focused ? FocusedButtonAttribute : ButtonAttribute;
        const bool background = tinyos::ui::renderer::fill_rect(0, draw_row, ButtonWidth, 1, ' ', attribute);
        const bool prefix = tinyos::ui::renderer::draw_text(1, draw_row, focused ? ">" : " ", attribute);
        const bool label = tinyos::ui::renderer::draw_text(3, draw_row, text, attribute);

        if (!background || !prefix || !label)
        {
            ++g_rejected_draws;
            return false;
        }

        ++g_button_draws;
        return true;
    }

    bool handle_event(const tinyos::ui::events::Event& event)
    {
        if (!g_state.ready || event.type != tinyos::ui::events::EventType::Key || !event.pressed)
        {
            ++g_rejected_draws;
            return false;
        }

        ++g_handled_events;
        if (event.character == ' ' || event.character == '\n')
        {
            ++g_activations;
            return draw_button(4, "Activated", true);
        }

        return true;
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

    bool render_demo()
    {
        if (!tinyos::ui::terminal::clear_content())
        {
            return false;
        }

        if (!tinyos::ui::terminal::draw_panel(0, 7, "TinyOS widget demo"))
        {
            return false;
        }

        if (!draw_label(2, "Label widget ready"))
        {
            return false;
        }

        return draw_button(4, "Activate", true);
    }

    uint64_t label_draw_count()
    {
        return g_label_draws;
    }

    uint64_t button_draw_count()
    {
        return g_button_draws;
    }

    uint64_t handled_event_count()
    {
        return g_handled_events;
    }

    uint64_t activation_count()
    {
        return g_activations;
    }

    uint64_t rejected_draw_count()
    {
        return g_rejected_draws;
    }

    bool validation_self_test()
    {
        return g_state.ready &&
            tinyos::ui::terminal::panel_validation_self_test() &&
            g_state.columns >= ButtonWidth &&
            g_state.content_rows >= 7;
    }

    bool event_bridge_validation_self_test()
    {
        return validation_self_test() && tinyos::ui::events::validation_self_test();
    }

    const char* kind_name(Kind kind)
    {
        switch (kind)
        {
        case Kind::Label:
            return "label";
        case Kind::Button:
            return "button";
        }

        return "unknown";
    }
}