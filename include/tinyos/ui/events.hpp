#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::ui::events
{
    enum class EventType : uint32_t
    {
        None,
        Key,
        Pointer,
        MouseButton
    };

    enum class Source : uint32_t
    {
        None,
        Keyboard,
        Mouse,
        Synthetic
    };

    struct Event
    {
        EventType type;
        Source source;
        char character;
        bool pressed;
        uint32_t column;
        uint32_t row;
        int32_t delta_column;
        int32_t delta_row;
        uint8_t button;
        uint64_t sequence;
    };

    void initialize();
    bool is_ready();
    bool push_key_event(char character, bool pressed);
    bool push_pointer_event(uint32_t column, uint32_t row, int32_t delta_column, int32_t delta_row);
    bool push_mouse_button_event(uint32_t column, uint32_t row, uint8_t button, bool pressed);
    size_t pump_from_input(size_t max_events);
    bool poll_event(Event& event);
    size_t pending_count();
    size_t queue_capacity();
    uint64_t pushed_event_count();
    uint64_t pumped_input_event_count();
    uint64_t polled_event_count();
    uint64_t dropped_event_count();
    bool validation_self_test();
    const char* event_type_name(EventType type);
    const char* source_name(Source source);
}