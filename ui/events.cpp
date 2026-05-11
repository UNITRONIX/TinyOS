#include <stddef.h>
#include <stdint.h>

#include <tinyos/drivers/input.hpp>
#include <tinyos/ui/events.hpp>

namespace
{
    constexpr size_t QueueSize = 64;

    tinyos::ui::events::Event g_queue[QueueSize] = {};
    volatile size_t g_read_index = 0;
    volatile size_t g_write_index = 0;
    volatile size_t g_count = 0;
    volatile bool g_ready = false;
    uint64_t g_next_sequence = 1;
    uint64_t g_pushed_events = 0;
    uint64_t g_pumped_input_events = 0;
    uint64_t g_polled_events = 0;
    uint64_t g_dropped_events = 0;

    void clear_event(tinyos::ui::events::Event& event)
    {
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
    }

    bool push_event(tinyos::ui::events::EventType type, tinyos::ui::events::Source source, char character, bool pressed, uint32_t column, uint32_t row, int32_t delta_column, int32_t delta_row, uint8_t button)
    {
        if (!g_ready || type == tinyos::ui::events::EventType::None || g_count >= QueueSize)
        {
            ++g_dropped_events;
            return false;
        }

        auto& event = g_queue[g_write_index];
        event.type = type;
        event.source = source;
        event.character = character;
        event.pressed = pressed;
        event.column = column;
        event.row = row;
        event.delta_column = delta_column;
        event.delta_row = delta_row;
        event.button = button;
        event.sequence = g_next_sequence;
        ++g_next_sequence;
        g_write_index = (g_write_index + 1) % QueueSize;
        ++g_count;
        ++g_pushed_events;
        return true;
    }
}

namespace tinyos::ui::events
{
    void initialize()
    {
        g_read_index = 0;
        g_write_index = 0;
        g_count = 0;
        g_ready = true;
        g_next_sequence = 1;
        g_pushed_events = 0;
        g_pumped_input_events = 0;
        g_polled_events = 0;
        g_dropped_events = 0;
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool push_key_event(char character, bool pressed)
    {
        if (character == 0)
        {
            ++g_dropped_events;
            return false;
        }

        return push_event(EventType::Key, Source::Synthetic, character, pressed, 0, 0, 0, 0, 0);
    }

    bool push_pointer_event(uint32_t column, uint32_t row, int32_t delta_column, int32_t delta_row)
    {
        return push_event(EventType::Pointer, Source::Synthetic, 0, false, column, row, delta_column, delta_row, 0);
    }

    bool push_mouse_button_event(uint32_t column, uint32_t row, uint8_t button, bool pressed)
    {
        return push_event(EventType::MouseButton, Source::Synthetic, 0, pressed, column, row, 0, 0, button);
    }

    size_t pump_from_input(size_t max_events)
    {
        if (!g_ready)
        {
            return 0;
        }

        size_t pumped = 0;
        while (pumped < max_events && g_count < QueueSize)
        {
            tinyos::drivers::input::Event input_event;
            input_event.type = tinyos::drivers::input::EventType::None;
            input_event.character = 0;
            input_event.pressed = false;
            input_event.column = 0;
            input_event.row = 0;
            input_event.delta_column = 0;
            input_event.delta_row = 0;
            input_event.button = 0;

            if (!tinyos::drivers::input::poll_event(input_event))
            {
                break;
            }

            if (input_event.type == tinyos::drivers::input::EventType::Key && input_event.character != 0)
            {
                if (push_event(EventType::Key, Source::Keyboard, input_event.character, input_event.pressed, 0, 0, 0, 0, 0))
                {
                    ++g_pumped_input_events;
                    ++pumped;
                }
            }
            else if (input_event.type == tinyos::drivers::input::EventType::Pointer)
            {
                if (push_event(EventType::Pointer, Source::Mouse, 0, false, input_event.column, input_event.row, input_event.delta_column, input_event.delta_row, 0))
                {
                    ++g_pumped_input_events;
                    ++pumped;
                }
            }
            else if (input_event.type == tinyos::drivers::input::EventType::MouseButton)
            {
                if (push_event(EventType::MouseButton, Source::Mouse, 0, input_event.pressed, input_event.column, input_event.row, 0, 0, input_event.button))
                {
                    ++g_pumped_input_events;
                    ++pumped;
                }
            }
            else
            {
                ++g_dropped_events;
            }
        }

        return pumped;
    }

    bool poll_event(Event& event)
    {
        if (g_count == 0)
        {
            clear_event(event);
            return false;
        }

        event = g_queue[g_read_index];
        g_read_index = (g_read_index + 1) % QueueSize;
        --g_count;
        ++g_polled_events;
        return true;
    }

    size_t pending_count()
    {
        return g_count;
    }

    size_t queue_capacity()
    {
        return QueueSize;
    }

    uint64_t pushed_event_count()
    {
        return g_pushed_events;
    }

    uint64_t pumped_input_event_count()
    {
        return g_pumped_input_events;
    }

    uint64_t polled_event_count()
    {
        return g_polled_events;
    }

    uint64_t dropped_event_count()
    {
        return g_dropped_events;
    }

    bool validation_self_test()
    {
        return g_ready &&
            QueueSize == 64 &&
            g_count <= QueueSize &&
            g_next_sequence != 0;
    }

    const char* event_type_name(EventType type)
    {
        switch (type)
        {
        case EventType::None:
            return "none";
        case EventType::Key:
            return "key";
        case EventType::Pointer:
            return "pointer";
        case EventType::MouseButton:
            return "mouse-button";
        }

        return "unknown";
    }

    const char* source_name(Source source)
    {
        switch (source)
        {
        case Source::None:
            return "none";
        case Source::Keyboard:
            return "keyboard";
        case Source::Mouse:
            return "mouse";
        case Source::Synthetic:
            return "synthetic";
        }

        return "unknown";
    }
}