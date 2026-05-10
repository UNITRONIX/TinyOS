#pragma once

#include <stdint.h>

namespace tinyos::arch::io
{
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t value);
    void io_wait();
}
