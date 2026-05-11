#include <stddef.h>
#include <stdint.h>

#include <tinyos/drivers/input.hpp>

namespace
{
    constexpr size_t QueueSize = 64;

    tinyos::drivers::input::Event g_queue[QueueSize] = {};
    volatile size_t g_read_index = 0;
    volatile size_t g_write_index = 0;
    volatile size_t g_count = 0;
    volatile uint64_t g_dropped_events = 0;

    void clear_event(tinyos::drivers::input::Event& event)
    {
        event.type = tinyos::drivers::input::EventType::None;
        event.character = 0;
        event.pressed = false;
        event.column = 0;
        event.row = 0;
        event.delta_column = 0;
        event.delta_row = 0;
        event.button = 0;
    }

    bool push_event(const tinyos::drivers::input::Event& event)
    {
        if (event.type == tinyos::drivers::input::EventType::None || g_count >= QueueSize)
        {
            ++g_dropped_events;
            return false;
        }

        g_queue[g_write_index] = event;
        g_write_index = (g_write_index + 1) % QueueSize;
        ++g_count;
        return true;
    }
}

namespace tinyos::drivers::input
{
    void initialize()
    {
        g_read_index = 0;
        g_write_index = 0;
        g_count = 0;
        g_dropped_events = 0;
    }

    void push_key_event(char character, bool pressed)
    {
        if (character == 0)
        {
            ++g_dropped_events;
            return;
        }

        Event event;
        clear_event(event);
        event.type = EventType::Key;
        event.character = character;
        event.pressed = pressed;
        push_event(event);
    }

    void push_pointer_event(uint32_t column, uint32_t row, int32_t delta_column, int32_t delta_row)
    {
        Event event;
        clear_event(event);
        event.type = EventType::Pointer;
        event.column = column;
        event.row = row;
        event.delta_column = delta_column;
        event.delta_row = delta_row;
        push_event(event);
    }

    void push_mouse_button_event(uint32_t column, uint32_t row, uint8_t button, bool pressed)
    {
        Event event;
        clear_event(event);
        event.type = EventType::MouseButton;
        event.column = column;
        event.row = row;
        event.button = button;
        event.pressed = pressed;
        push_event(event);
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

    uint64_t dropped_event_count()
    {
        return g_dropped_events;
    }
}
