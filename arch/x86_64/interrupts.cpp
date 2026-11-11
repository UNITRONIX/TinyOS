#include <stddef.h>
#include <stdint.h>

#include <tinyos/arch/x86_64/gdt.hpp>
#include <tinyos/arch/x86_64/interrupts.hpp>
#include <tinyos/drivers/serial.hpp>

namespace
{
    struct [[gnu::packed]] IdtEntry
    {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t ist;
        uint8_t type_attributes;
        uint16_t offset_middle;
        uint32_t offset_high;
        uint32_t reserved;
    };

    struct [[gnu::packed]] IdtPointer
    {
        uint16_t limit;
        uint64_t base;
    };

    constexpr uint8_t InterruptGate = 0x8E;
    constexpr uint8_t TrapGate = 0x8F;
    constexpr uint8_t UserInterruptGate = 0xEE;

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

    IdtEntry g_idt[256] = {};

    void (*g_handlers[256])(uint64_t, uint64_t, const tinyos::arch::x86_64::interrupts::FaultFrame*) = {};

    uint64_t g_fault_count = 0;
    uint64_t g_spurious_count = 0;
    uint64_t g_last_fault_vector = 0;
    bool g_ready = false;

    uint64_t read_cr2()
    {
        uint64_t value = 0;
        asm volatile ("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    void write_hex64(uint64_t value)
    {
        constexpr char Digits[] = "0123456789ABCDEF";
        char buffer[19];
        buffer[0] = '0';
        buffer[1] = 'x';
        for (int index = 0; index < 16; ++index)
        {
            const int shift = (15 - index) * 4;
            buffer[index + 2] = Digits[(value >> shift) & 0xF];
        }
        buffer[18] = '\0';
        tinyos::drivers::serial::write_line(buffer);
    }

    void set_gate(uint8_t vector, uint64_t handler, uint8_t ist, uint8_t attributes)
    {
        g_idt[vector].offset_low = static_cast<uint16_t>(handler & 0xFFFF);
        g_idt[vector].selector = tinyos::arch::x86_64::gdt::KernelCodeSelector;
        g_idt[vector].ist = static_cast<uint8_t>(ist & 0x7);
        g_idt[vector].type_attributes = attributes;
        g_idt[vector].offset_middle = static_cast<uint16_t>((handler >> 16) & 0xFFFF);
        g_idt[vector].offset_high = static_cast<uint32_t>(handler >> 32);
        g_idt[vector].reserved = 0;
    }

    extern "C" void idt_load(const IdtPointer* pointer);
}

extern "C" void* interrupt_stub_table[];

extern "C" void interrupt_dispatch64(uint64_t vector, uint64_t error_code, const tinyos::arch::x86_64::interrupts::FaultFrame* frame)
{
    // A spurious stub reports 0xFF; nothing else uses that value.
    if (vector == 0xFF)
    {
        ++g_spurious_count;
        tinyos::drivers::serial::write_line("[intr] spurious interrupt");
        return;
    }

    if (vector < 32)
    {
        ++g_fault_count;
        g_last_fault_vector = vector;

        tinyos::drivers::serial::write_line("CPU EXCEPTION");
        tinyos::drivers::serial::write_line(tinyos::arch::x86_64::interrupts::exception_name(vector));

        tinyos::drivers::serial::write("vector  : ");
        write_hex64(vector);
        tinyos::drivers::serial::write("error   : ");
        write_hex64(error_code);
        if (frame != nullptr)
        {
            tinyos::drivers::serial::write("rip     : ");
            write_hex64(frame->rip);
            tinyos::drivers::serial::write("cs      : ");
            write_hex64(frame->cs);
            tinyos::drivers::serial::write("rflags  : ");
            write_hex64(frame->rflags);
            tinyos::drivers::serial::write("rsp     : ");
            write_hex64(frame->rsp);
        }

        if (vector == 14)
        {
            tinyos::drivers::serial::write("cr2     : ");
            write_hex64(read_cr2());
        }

        // Exceptions other than a page fault that the kernel might recover from
        // are fatal here: without a process to kill, continuing means executing
        // corrupted state.
        if (vector == 14)
        {
            // Page faults are reported and then also halt until there is a
            // demand-paging fault handler to resolve them.
        }

        asm volatile ("cli");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    if (g_handlers[vector] != nullptr)
    {
        g_handlers[vector](vector, error_code, frame);
    }
}

namespace tinyos::arch::x86_64::interrupts
{
    void initialize()
    {
        for (size_t vector = 0; vector < 256; ++vector)
        {
            const uint64_t handler = reinterpret_cast<uint64_t>(interrupt_stub_table[vector]);

            // Double fault and machine check must run on their own stacks: the
            // faulting stack is exactly what is suspect.
            uint8_t ist = 0;
            if (vector == 8)
            {
                ist = tinyos::arch::x86_64::gdt::IstDoubleFault;
            }
            else if (vector == 2)
            {
                ist = tinyos::arch::x86_64::gdt::IstNmi;
            }
            else if (vector == 18)
            {
                ist = tinyos::arch::x86_64::gdt::IstMachineCheck;
            }

            set_gate(static_cast<uint8_t>(vector), handler, ist, InterruptGate);
        }

        // Vector 3 (breakpoint) is a trap: returning must not re-raise it.
        set_gate(3, reinterpret_cast<uint64_t>(interrupt_stub_table[3]), 0, TrapGate);

        // System call vector for user processes. It is a user-callable interrupt
        // gate so ring 3 can reach the kernel.
        set_gate(0x80, reinterpret_cast<uint64_t>(interrupt_stub_table[0x80]), 0, UserInterruptGate);

        const IdtPointer pointer = {
            static_cast<uint16_t>(sizeof(g_idt) - 1),
            reinterpret_cast<uint64_t>(&g_idt[0])
        };

        idt_load(&pointer);
        g_ready = true;
    }

    void enable()
    {
        asm volatile ("sti" ::: "memory");
    }

    void disable()
    {
        asm volatile ("cli" ::: "memory");
    }

    bool interrupts_enabled()
    {
        uint64_t flags = 0;
        asm volatile ("pushfq; pop %0" : "=r"(flags));
        return (flags & 0x200) != 0;
    }

    bool install_handler(uint8_t vector, void (*handler)(uint64_t, uint64_t, const FaultFrame*))
    {
        if (vector < 32)
        {
            // CPU exceptions are owned by the dispatcher above; installing a
            // recovery callback for them is a later feature.
            return false;
        }

        g_handlers[vector] = handler;
        return true;
    }

    uint64_t fault_count()
    {
        return g_fault_count;
    }

    uint64_t spurious_count()
    {
        return g_spurious_count;
    }

    uint64_t last_fault_vector()
    {
        return g_last_fault_vector;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        // Verify every gate points at a real handler with the expected selector.
        for (size_t vector = 0; vector < 256; ++vector)
        {
            if (g_idt[vector].selector != tinyos::arch::x86_64::gdt::KernelCodeSelector)
            {
                return false;
            }

            if (g_idt[vector].offset_low == 0 && g_idt[vector].offset_middle == 0 && g_idt[vector].offset_high == 0)
            {
                return false;
            }
        }

        return g_idt[8].ist == tinyos::arch::x86_64::gdt::IstDoubleFault;
    }

    const char* exception_name(uint64_t vector)
    {
        if (vector < 32)
        {
            return ExceptionNames[vector];
        }

        return "hardware interrupt";
    }
}
