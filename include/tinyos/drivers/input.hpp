#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::drivers::input
{
    enum class EventType
    {
        None,
        Key,
        Pointer,
        MouseButton
    };

    struct Event
    {
        EventType type;
        char character;
        bool pressed;
        uint32_t column;
        uint32_t row;
        int32_t delta_column;
        int32_t delta_row;
        uint8_t button;
    };

    void initialize();
    void push_key_event(char character, bool pressed);
    void push_pointer_event(uint32_t column, uint32_t row, int32_t delta_column, int32_t delta_row);
    void push_mouse_button_event(uint32_t column, uint32_t row, uint8_t button, bool pressed);
    bool poll_event(Event& event);
    size_t pending_count();
    size_t queue_capacity();
    uint64_t dropped_event_count();
}
