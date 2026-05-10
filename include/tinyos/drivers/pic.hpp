#pragma once

#include <stdint.h>

namespace tinyos::drivers::pic
{
    void initialize();
    void set_mask(uint8_t irq);
    void clear_mask(uint8_t irq);
    bool is_initialized();
    bool is_masked(uint8_t irq);
    void send_eoi(uint8_t irq);
}
