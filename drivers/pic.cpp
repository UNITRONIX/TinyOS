#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/pic.hpp>

namespace
{
    constexpr uint16_t MasterCommandPort = 0x20;
    constexpr uint16_t MasterDataPort = 0x21;
    constexpr uint16_t SlaveCommandPort = 0xA0;
    constexpr uint16_t SlaveDataPort = 0xA1;

    constexpr uint8_t Icw1Init = 0x10;
    constexpr uint8_t Icw1Icw4 = 0x01;
    constexpr uint8_t Icw4_8086 = 0x01;

    bool g_initialized = false;
    uint8_t g_master_mask = 0xFF;
    uint8_t g_slave_mask = 0xFF;

    uint16_t data_port_for_irq(uint8_t irq)
    {
        return irq < 8 ? MasterDataPort : SlaveDataPort;
    }

    uint8_t bit_for_irq(uint8_t irq)
    {
        return static_cast<uint8_t>(1u << (irq % 8));
    }

    uint8_t& shadow_mask_for_irq(uint8_t irq)
    {
        return irq < 8 ? g_master_mask : g_slave_mask;
    }

    void write_mask(uint8_t irq)
    {
        tinyos::arch::io::outb(data_port_for_irq(irq), shadow_mask_for_irq(irq));
    }
}

namespace tinyos::drivers::pic
{
    void initialize()
    {
        arch::io::outb(MasterCommandPort, Icw1Init | Icw1Icw4);
        arch::io::io_wait();
        arch::io::outb(SlaveCommandPort, Icw1Init | Icw1Icw4);
        arch::io::io_wait();

        arch::io::outb(MasterDataPort, 0x20);
        arch::io::io_wait();
        arch::io::outb(SlaveDataPort, 0x28);
        arch::io::io_wait();

        arch::io::outb(MasterDataPort, 0x04);
        arch::io::io_wait();
        arch::io::outb(SlaveDataPort, 0x02);
        arch::io::io_wait();

        arch::io::outb(MasterDataPort, Icw4_8086);
        arch::io::io_wait();
        arch::io::outb(SlaveDataPort, Icw4_8086);
        arch::io::io_wait();

        g_master_mask = 0xFF;
        g_slave_mask = 0xFF;
        arch::io::outb(MasterDataPort, g_master_mask);
        arch::io::outb(SlaveDataPort, g_slave_mask);
        g_initialized = true;
    }

    void set_mask(uint8_t irq)
    {
        if (irq >= 16)
        {
            return;
        }

        shadow_mask_for_irq(irq) = static_cast<uint8_t>(shadow_mask_for_irq(irq) | bit_for_irq(irq));
        write_mask(irq);
    }

    void clear_mask(uint8_t irq)
    {
        if (irq >= 16)
        {
            return;
        }

        if (irq >= 8)
        {
            g_master_mask = static_cast<uint8_t>(g_master_mask & ~bit_for_irq(2));
            arch::io::outb(MasterDataPort, g_master_mask);
        }

        shadow_mask_for_irq(irq) = static_cast<uint8_t>(shadow_mask_for_irq(irq) & ~bit_for_irq(irq));
        write_mask(irq);
    }

    bool is_initialized()
    {
        return g_initialized;
    }

    bool is_masked(uint8_t irq)
    {
        if (irq >= 16)
        {
            return true;
        }

        return (shadow_mask_for_irq(irq) & bit_for_irq(irq)) != 0;
    }

    void send_eoi(uint8_t irq)
    {
        if (irq >= 8)
        {
            arch::io::outb(SlaveCommandPort, 0x20);
        }

        arch::io::outb(MasterCommandPort, 0x20);
    }
}
