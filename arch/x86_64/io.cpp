#include <tinyos/arch/io.hpp>

namespace tinyos::arch::io
{
    uint8_t inb(uint16_t port)
    {
        uint8_t value;
        asm volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    void outb(uint16_t port, uint8_t value)
    {
        asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    uint16_t inw(uint16_t port)
    {
        uint16_t value;
        asm volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    void outw(uint16_t port, uint16_t value)
    {
        asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
    }

    uint32_t inl(uint16_t port)
    {
        uint32_t value;
        asm volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
        return value;
    }

    void outl(uint16_t port, uint32_t value)
    {
        asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
    }

    void io_wait()
    {
        // Port 0x80 is unused on every PC the kernel targets and is the
        // traditional way to insert a short delay on an ISA bus transaction.
        asm volatile ("outb %%al, $0x80" : : "a"(static_cast<uint8_t>(0)));
    }
}
