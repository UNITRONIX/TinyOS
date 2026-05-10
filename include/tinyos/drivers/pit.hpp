#pragma once

#include <stdint.h>

namespace tinyos::drivers::pit
{
    void initialize(uint32_t frequency_hz);
    void handle_irq();
    bool is_configured();
    uint64_t ticks();
    uint32_t frequency();
}
