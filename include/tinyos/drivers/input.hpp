#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::drivers::input
{
    enum class EventType
    {
        None,
        Key
    };

    struct Event
    {
        EventType type;
        char character;
        bool pressed;
    };

    void initialize();
    void push_key_event(char character, bool pressed);
    bool poll_event(Event& event);
    size_t pending_count();
    size_t queue_capacity();
    uint64_t dropped_event_count();
}
