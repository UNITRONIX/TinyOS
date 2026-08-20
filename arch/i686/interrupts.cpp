#include <stdint.h>

#include <tinyos/arch/interrupts.hpp>

namespace
{
    struct [[gnu::packed]] IdtEntry
    {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t zero;
        uint8_t type_attributes;
        uint16_t offset_high;
    };

    struct [[gnu::packed]] IdtDescriptor
    {
        uint16_t size;
        uint32_t offset;
    };

    constexpr uint8_t InterruptGate = 0x8E;
    constexpr uint8_t UserInterruptGate = 0xEE;

    IdtEntry g_idt[256] = {};
    uint16_t g_kernel_code_selector = 0;

    extern "C" void* isr_stub_table[];
    extern "C" void* irq_stub_table[];
    extern "C" void syscall_stub();
    extern "C" void idt_load(const IdtDescriptor* descriptor);

    uint16_t current_code_selector()
    {
        uint16_t selector = 0;
        asm volatile ("mov %%cs, %0" : "=r"(selector));
        return selector;
    }

    void set_gate(int vector, void (*handler)(), uint8_t type_attributes)
    {
        const uint32_t address = reinterpret_cast<uint32_t>(handler);
        g_idt[vector].offset_low = static_cast<uint16_t>(address & 0xFFFF);
        g_idt[vector].selector = g_kernel_code_selector;
        g_idt[vector].zero = 0;
        g_idt[vector].type_attributes = type_attributes;
        g_idt[vector].offset_high = static_cast<uint16_t>((address >> 16) & 0xFFFF);
    }
}

namespace tinyos::arch::interrupts
{
    void initialize()
    {
        g_kernel_code_selector = current_code_selector();

        for (int vector = 0; vector < 32; ++vector)
        {
            set_gate(vector, reinterpret_cast<void (*)()>(isr_stub_table[vector]), InterruptGate);
        }

        for (int irq = 0; irq < 16; ++irq)
        {
            set_gate(32 + irq, reinterpret_cast<void (*)()>(irq_stub_table[irq]), InterruptGate);
        }

        set_gate(0x80, syscall_stub, UserInterruptGate);

        const IdtDescriptor descriptor = {
            static_cast<uint16_t>(sizeof(g_idt) - 1),
            reinterpret_cast<uint32_t>(&g_idt[0])
        };

        idt_load(&descriptor);
    }

    void enable()
    {
        asm volatile ("sti");
    }

    void disable()
    {
        asm volatile ("cli");
    }
}
