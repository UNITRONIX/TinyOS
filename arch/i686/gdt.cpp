#include <stddef.h>
#include <stdint.h>

#include <tinyos/arch/gdt.hpp>

namespace
{
    struct [[gnu::packed]] GdtEntry
    {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t base_middle;
        uint8_t access;
        uint8_t granularity;
        uint8_t base_high;
    };

    struct [[gnu::packed]] GdtDescriptor
    {
        uint16_t size;
        uint32_t offset;
    };

    struct [[gnu::packed]] Tss
    {
        uint32_t prev_tss;
        uint32_t esp0;
        uint32_t ss0;
        uint32_t esp1;
        uint32_t ss1;
        uint32_t esp2;
        uint32_t ss2;
        uint32_t cr3;
        uint32_t eip;
        uint32_t eflags;
        uint32_t eax;
        uint32_t ecx;
        uint32_t edx;
        uint32_t ebx;
        uint32_t esp;
        uint32_t ebp;
        uint32_t esi;
        uint32_t edi;
        uint32_t es;
        uint32_t cs;
        uint32_t ss;
        uint32_t ds;
        uint32_t fs;
        uint32_t gs;
        uint32_t ldt;
        uint16_t trap;
        uint16_t iomap_base;
    };

    constexpr uint16_t KernelCodeSelector = 0x08;
    constexpr uint16_t KernelDataSelector = 0x10;
    constexpr uint16_t UserCodeSelector = 0x18 | 0x3;
    constexpr uint16_t UserDataSelector = 0x20 | 0x3;
    constexpr uint16_t TssSelector = 0x28;

    GdtEntry g_gdt[6] = {};
    Tss g_tss = {};
    bool g_ready = false;

    void set_entry(GdtEntry& entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
    {
        entry.limit_low = static_cast<uint16_t>(limit & 0xFFFF);
        entry.base_low = static_cast<uint16_t>(base & 0xFFFF);
        entry.base_middle = static_cast<uint8_t>((base >> 16) & 0xFF);
        entry.access = access;
        entry.granularity = static_cast<uint8_t>(((limit >> 16) & 0x0F) | (gran & 0xF0));
        entry.base_high = static_cast<uint8_t>((base >> 24) & 0xFF);
    }

    extern "C" void gdt_flush(const GdtDescriptor* descriptor);
    extern "C" void tss_flush(uint16_t selector);
}

namespace tinyos::arch::gdt
{
    void initialize()
    {
        set_entry(g_gdt[0], 0, 0, 0, 0);
        set_entry(g_gdt[1], 0, 0xFFFFF, 0x9A, 0xC0); // kernel code
        set_entry(g_gdt[2], 0, 0xFFFFF, 0x92, 0xC0); // kernel data
        set_entry(g_gdt[3], 0, 0xFFFFF, 0xFA, 0xC0); // user code
        set_entry(g_gdt[4], 0, 0xFFFFF, 0xF2, 0xC0); // user data

        const uint32_t tss_base = reinterpret_cast<uint32_t>(&g_tss);
        set_entry(g_gdt[5], tss_base, sizeof(Tss) - 1, 0x89, 0x00);

        for (size_t index = 0; index < sizeof(Tss); ++index)
        {
            reinterpret_cast<uint8_t*>(&g_tss)[index] = 0;
        }

        g_tss.ss0 = KernelDataSelector;
        g_tss.esp0 = 0;
        g_tss.iomap_base = sizeof(Tss);

        const GdtDescriptor descriptor = {
            static_cast<uint16_t>(sizeof(g_gdt) - 1),
            reinterpret_cast<uint32_t>(&g_gdt[0])
        };

        gdt_flush(&descriptor);
        tss_flush(TssSelector);
        g_ready = true;
    }

    bool is_ready()
    {
        return g_ready;
    }

    uint16_t kernel_code_selector()
    {
        return KernelCodeSelector;
    }

    uint16_t kernel_data_selector()
    {
        return KernelDataSelector;
    }

    uint16_t user_code_selector()
    {
        return UserCodeSelector;
    }

    uint16_t user_data_selector()
    {
        return UserDataSelector;
    }

    uint16_t tss_selector()
    {
        return TssSelector;
    }

    void set_kernel_stack(uint32_t stack_top)
    {
        g_tss.esp0 = stack_top;
    }

    bool validation_self_test()
    {
        return g_ready &&
            (UserCodeSelector & 0x3) == 0x3 &&
            (UserDataSelector & 0x3) == 0x3 &&
            g_tss.ss0 == KernelDataSelector;
    }
}
