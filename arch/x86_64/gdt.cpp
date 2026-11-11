#include <stddef.h>
#include <stdint.h>

#include <tinyos/arch/x86_64/gdt.hpp>

namespace
{
    // In 64-bit mode the segment base and limit fields are ignored, but the
    // layout is unchanged and the access/granularity bytes still matter.
    struct [[gnu::packed]] GdtEntry
    {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t base_middle;
        uint8_t access;
        uint8_t granularity;
        uint8_t base_high;
    };

    // A TSS descriptor in 64-bit mode is 16 bytes: the extra 8 bytes hold the
    // upper half of the base address, which does not fit in the legacy entry.
    struct [[gnu::packed]] TssDescriptor
    {
        GdtEntry legacy;
        uint32_t base_upper;
        uint32_t reserved;
    };

    struct [[gnu::packed]] GdtPointer
    {
        uint16_t limit;
        uint64_t base;
    };

    struct [[gnu::packed]] Tss
    {
        uint32_t reserved0;
        uint64_t rsp0;
        uint64_t rsp1;
        uint64_t rsp2;
        uint64_t reserved1;
        uint64_t ist[7];
        uint64_t reserved2;
        uint16_t reserved3;
        uint16_t iomap_base;
    };

    constexpr size_t GdtEntryCount = 7;
    constexpr size_t IstStackBytes = 8192;

    GdtEntry g_gdt[GdtEntryCount] = {};
    Tss g_tss = {};

    // IST stacks must be 16-byte aligned and are never shared between slots.
    alignas(16) uint8_t g_ist_double_fault[IstStackBytes];
    alignas(16) uint8_t g_ist_nmi[IstStackBytes];
    alignas(16) uint8_t g_ist_machine_check[IstStackBytes];

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

    void set_tss_descriptor(TssDescriptor& descriptor, const Tss* tss)
    {
        const uint64_t base = reinterpret_cast<uint64_t>(tss);
        const uint32_t limit = sizeof(Tss) - 1;

        set_entry(descriptor.legacy, static_cast<uint32_t>(base & 0xFFFFFFFFu), limit, 0x89, 0x00);
        descriptor.base_upper = static_cast<uint32_t>(base >> 32);
        descriptor.reserved = 0;
    }

    uint64_t top_of_stack(uint8_t* base)
    {
        const uint64_t top = reinterpret_cast<uint64_t>(base) + IstStackBytes;
        return top & ~static_cast<uint64_t>(0xF);
    }
}

namespace tinyos::arch::x86_64::gdt
{
    void initialize()
    {
        // Null descriptor.
        set_entry(g_gdt[0], 0, 0, 0, 0);

        // Long mode code/data: present, DPL 0, code/data bits, L=1 for code.
        set_entry(g_gdt[1], 0, 0, 0x9A, 0xA0); // 0x08 kernel code (L=1, gran 4K)
        set_entry(g_gdt[2], 0, 0, 0x92, 0xC0); // 0x10 kernel data
        set_entry(g_gdt[3], 0, 0, 0xFA, 0xA0); // 0x18 user code
        set_entry(g_gdt[4], 0, 0, 0xF2, 0xC0); // 0x20 user data

        for (size_t index = 0; index < sizeof(Tss); ++index)
        {
            reinterpret_cast<uint8_t*>(&g_tss)[index] = 0;
        }

        g_tss.ist[IstDoubleFault - 1] = top_of_stack(g_ist_double_fault);
        g_tss.ist[IstNmi - 1] = top_of_stack(g_ist_nmi);
        g_tss.ist[IstMachineCheck - 1] = top_of_stack(g_ist_machine_check);
        g_tss.iomap_base = sizeof(Tss);

        // The TSS descriptor occupies two adjacent GDT slots, so it is written
        // as a 16-byte structure starting at entry 5.
        auto* descriptor = reinterpret_cast<TssDescriptor*>(&g_gdt[5]);
        set_tss_descriptor(*descriptor, &g_tss);

        const GdtPointer pointer = {
            static_cast<uint16_t>(sizeof(g_gdt) - 1),
            reinterpret_cast<uint64_t>(&g_gdt[0])
        };

        asm volatile ("lgdt %0" : : "m"(pointer) : "memory");
        asm volatile (
            "push %[code]\n"
            "lea 1f(%%rip), %%rax\n"
            "push %%rax\n"
            "lretq\n"
            "1:\n"
            "mov %[data], %%ax\n"
            "mov %%ax, %%ds\n"
            "mov %%ax, %%es\n"
            "mov %%ax, %%fs\n"
            "mov %%ax, %%gs\n"
            "mov %%ax, %%ss\n"
            :
            : [code] "i"(KernelCodeSelector), [data] "i"(KernelDataSelector)
            : "rax", "memory");

        asm volatile ("ltr %0" : : "r"(static_cast<uint16_t>(TssSelector)));

        g_ready = true;
    }

    bool is_ready()
    {
        return g_ready;
    }

    void set_kernel_stack(uint64_t stack_top)
    {
        g_tss.rsp0 = stack_top;
    }

    uint64_t ist_stack_top(uint8_t index)
    {
        switch (index)
        {
        case IstDoubleFault:
            return g_tss.ist[IstDoubleFault - 1];
        case IstNmi:
            return g_tss.ist[IstNmi - 1];
        case IstMachineCheck:
            return g_tss.ist[IstMachineCheck - 1];
        }

        return 0;
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

    bool validation_self_test()
    {
        return g_ready &&
            (UserCodeSelector & 0x3) == 0x3 &&
            (UserDataSelector & 0x3) == 0x3 &&
            g_tss.ist[IstDoubleFault - 1] != 0 &&
            g_tss.ist[IstNmi - 1] != 0 &&
            g_tss.ist[IstMachineCheck - 1] != 0 &&
            (g_tss.ist[IstDoubleFault - 1] % 16) == 0;
    }
}
