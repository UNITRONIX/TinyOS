#pragma once

#include <stdint.h>

namespace tinyos::drivers::mouse
{
    void initialize();
    bool is_ready();
    void set_bounds(uint32_t width, uint32_t height);
    void handle_irq();
    uint32_t cursor_x();
    uint32_t cursor_y();
    bool left_button_down();
    uint64_t packet_count();
    uint64_t button_event_count();
    uint64_t dropped_packet_count();
    bool packet_decoder_self_test();
}