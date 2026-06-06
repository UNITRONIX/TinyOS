#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::drivers::keyboard
{
    constexpr char KeyLeft = 0x11;
    constexpr char KeyRight = 0x12;
    constexpr char KeyUp = 0x13;
    constexpr char KeyDown = 0x14;
    constexpr char KeyHome = 0x15;
    constexpr char KeyEnd = 0x16;
    constexpr char KeyDelete = 0x17;
    constexpr char KeyPgUp = 0x18;
    constexpr char KeyPgDn = 0x19;
    constexpr char KeyShiftTab = 0x1A;

    void initialize();
    void enable_interrupt_input();
    bool interrupt_input_enabled();
    void handle_irq();
    char read_char();
    void read_line(char* buffer, size_t max_length);
    bool is_special_key(char character);
    size_t buffered_character_count();
    uint64_t irq_scancode_count();
    uint64_t polled_scancode_count();
    uint64_t ignored_scancode_count();
    uint64_t dropped_character_count();
    bool has_seen_scancode();
    uint8_t last_scancode();
}
