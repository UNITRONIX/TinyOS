#include <stdint.h>

#include <tinyos/arch/hal.hpp>
#include <tinyos/drivers/keyboard.hpp>
#if !defined(TINYOS_TERMINAL_ONLY)
#include <tinyos/drivers/mouse.hpp>
#endif
#include <tinyos/drivers/pic.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/interrupts.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>

namespace
{
    constexpr const char* ExceptionNames[32] = {
        "Divide Error",
        "Debug",
        "Non Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "Bound Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Hypervisor Injection Exception",
        "VMM Communication Exception",
        "Security Exception",
        "Reserved"
    };

    volatile uint64_t g_irq_counts[tinyos::kernel::interrupts::IrqLineCount] = {};
    volatile uint64_t g_total_irq_count = 0;
    volatile uint64_t g_unexpected_irq_count = 0;
    volatile uint8_t g_last_irq = tinyos::kernel::interrupts::NoIrq;
    bool g_hardware_irq_enabled = false;

    void to_hex(uint32_t value, char* buffer)
    {
        constexpr char Digits[] = "0123456789ABCDEF";
        buffer[0] = '0';
        buffer[1] = 'x';

        for (int index = 0; index < 8; ++index)
        {
            const int shift = (7 - index) * 4;
            buffer[index + 2] = Digits[(value >> shift) & 0x0F];
        }

        buffer[10] = '\0';
    }

}

namespace tinyos::kernel::interrupts
{
    void initialize_diagnostics()
    {
        for (uint8_t irq = 0; irq < IrqLineCount; ++irq)
        {
            g_irq_counts[irq] = 0;
        }

        g_total_irq_count = 0;
        g_unexpected_irq_count = 0;
        g_last_irq = NoIrq;
        g_hardware_irq_enabled = false;
    }

    void record_irq(uint32_t irq)
    {
        ++g_total_irq_count;

        if (irq < IrqLineCount)
        {
            ++g_irq_counts[irq];
            g_last_irq = static_cast<uint8_t>(irq);
            return;
        }

        ++g_unexpected_irq_count;
        g_last_irq = NoIrq;
    }

    uint64_t irq_count(uint8_t irq)
    {
        if (irq >= IrqLineCount)
        {
            return 0;
        }

        return g_irq_counts[irq];
    }

    uint64_t total_irq_count()
    {
        return g_total_irq_count;
    }

    uint64_t unexpected_irq_count()
    {
        return g_unexpected_irq_count;
    }

    uint8_t last_irq()
    {
        return g_last_irq;
    }

    bool has_seen_irq()
    {
        return g_last_irq != NoIrq;
    }

    bool hardware_irq_enabled()
    {
        return g_hardware_irq_enabled;
    }

    void set_hardware_irq_enabled(bool enabled)
    {
        g_hardware_irq_enabled = enabled;
    }
}

extern "C" void interrupt_dispatch(uint32_t vector, uint32_t error_code)
{
    char vector_buffer[11];
    char error_buffer[11];

    to_hex(vector, vector_buffer);
    to_hex(error_code, error_buffer);

    tinyos::drivers::vga::set_color(tinyos::drivers::vga::Color::White, tinyos::drivers::vga::Color::Red);
    tinyos::drivers::vga::write_line("CPU EXCEPTION");
    tinyos::drivers::serial::write_line("CPU EXCEPTION");

    if (vector < 32)
    {
        tinyos::drivers::vga::write_line(ExceptionNames[vector]);
        tinyos::drivers::serial::write_line(ExceptionNames[vector]);
    }
    else
    {
        tinyos::drivers::vga::write_line("Unknown exception");
        tinyos::drivers::serial::write_line("Unknown exception");
    }

    tinyos::drivers::vga::write("Vector: ");
    tinyos::drivers::vga::write_line(vector_buffer);
    tinyos::drivers::vga::write("Error : ");
    tinyos::drivers::vga::write_line(error_buffer);

    tinyos::drivers::serial::write("Vector: ");
    tinyos::drivers::serial::write_line(vector_buffer);
    tinyos::drivers::serial::write("Error : ");
    tinyos::drivers::serial::write_line(error_buffer);

    tinyos::arch::halt();
}

extern "C" void irq_dispatch(uint32_t irq)
{
    tinyos::kernel::sched::irq_enter();
    tinyos::kernel::interrupts::record_irq(irq);

    switch (irq)
    {
    case 0:
        tinyos::drivers::pit::handle_irq();
        break;
    case 1:
        tinyos::drivers::keyboard::handle_irq();
        break;
#if !defined(TINYOS_TERMINAL_ONLY)
    case 12:
        tinyos::drivers::mouse::handle_irq();
        break;
#endif
    default:
        break;
    }

    tinyos::drivers::pic::send_eoi(static_cast<uint8_t>(irq));
    tinyos::kernel::sched::irq_leave();

    if (irq == 0)
    {
        tinyos::kernel::sched::irq_preempt_tail();
    }
}
