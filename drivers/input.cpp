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
        if (character == 0 || g_count >= QueueSize)
        {
            if (character != 0)
            {
                ++g_dropped_events;
            }
            return;
        }

        g_queue[g_write_index].type = EventType::Key;
        g_queue[g_write_index].character = character;
        g_queue[g_write_index].pressed = pressed;
        g_write_index = (g_write_index + 1) % QueueSize;
        ++g_count;
    }

    bool poll_event(Event& event)
    {
        if (g_count == 0)
        {
            event.type = EventType::None;
            event.character = 0;
            event.pressed = false;
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
