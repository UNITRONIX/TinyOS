#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>

namespace
{
    constexpr uint16_t CommandPort = 0x43;
    constexpr uint16_t Channel0Port = 0x40;
    constexpr uint32_t InputClockHz = 1193182;

    volatile uint64_t g_ticks = 0;
    uint32_t g_frequency_hz = 100;
    bool g_configured = false;
}

namespace tinyos::drivers::pit
{
    void initialize(uint32_t frequency_hz)
    {
        if (frequency_hz == 0)
        {
            frequency_hz = 100;
        }

        g_frequency_hz = frequency_hz;

        uint16_t divisor = static_cast<uint16_t>(InputClockHz / frequency_hz);
        if (divisor == 0)
        {
            divisor = 1;
        }

        arch::io::outb(CommandPort, 0x36);
        arch::io::outb(Channel0Port, static_cast<uint8_t>(divisor & 0xFF));
        arch::io::outb(Channel0Port, static_cast<uint8_t>((divisor >> 8) & 0xFF));
        g_configured = true;
    }

    void handle_irq()
    {
        ++g_ticks;
        tinyos::kernel::sched::on_timer_tick();
    }

    uint64_t ticks()
    {
        return g_ticks;
    }

    bool is_configured()
    {
        return g_configured;
    }

    uint32_t frequency()
    {
        return g_frequency_hz;
    }
}
