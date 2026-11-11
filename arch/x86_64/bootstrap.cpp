#include <stdint.h>

#include <tinyos/arch/x86_64/cpu.hpp>
#include <tinyos/arch/x86_64/gdt.hpp>
#include <tinyos/arch/x86_64/interrupts.hpp>
#include <tinyos/drivers/serial.hpp>

namespace
{
    // Multiboot2 information block. Only the fields the boot path reads are
    // modelled; tags are walked by total size, so an unmodelled tag is skipped
    // rather than misread.
    struct [[gnu::packed]] MultibootTag
    {
        uint32_t type;
        uint32_t size;
    };

    struct [[gnu::packed]] MultibootInfo
    {
        uint32_t total_size;
        uint32_t reserved;
        // Tags follow.
    };

    constexpr uint32_t MultibootBootloaderMagic = 0x36D76289;
    constexpr uint32_t TagEndOfList = 0;
    constexpr uint32_t TagBasicMemoryInfo = 4;
    constexpr uint32_t TagCommandLine = 1;

    constexpr const char* BootstrapVersion = "0.1.0-x86_64";

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

    void write_decimal(uint64_t value)
    {
        char buffer[21];
        buffer[20] = '\0';
        int index = 19;
        do
        {
            buffer[index] = static_cast<char>('0' + (value % 10));
            value /= 10;
            --index;
        } while (value != 0 && index >= 0);

        // write() emits no newline; callers decide where the line ends. Adding
        // one here split "Family/model/stepping: 15/107/1" across three lines.
        tinyos::drivers::serial::write(&buffer[index + 1]);
    }

    void write_feature(const char* name, bool present)
    {
        tinyos::drivers::serial::write("  ");
        tinyos::drivers::serial::write(name);
        tinyos::drivers::serial::write_line(present ? ": yes" : ": no");
    }

    // Walks the Multiboot2 tag list and reports total memory from the basic
    // memory info tag, so the boot log proves the handoff block was parsed
    // rather than assumed.
    uint64_t scan_multiboot_info(uint64_t info_address)
    {
        if (info_address == 0)
        {
            return 0;
        }

        const auto* info = reinterpret_cast<const MultibootInfo*>(info_address);
        if (info->total_size < sizeof(MultibootInfo) || info->total_size > (16u << 20))
        {
            return 0;
        }

        uint64_t memory_kib = 0;
        uint64_t offset = 8;
        while (offset + sizeof(MultibootTag) <= info->total_size)
        {
            const auto* tag = reinterpret_cast<const MultibootTag*>(info_address + offset);
            if (tag->type == TagEndOfList || tag->size < sizeof(MultibootTag))
            {
                break;
            }

            if (tag->type == TagBasicMemoryInfo && tag->size >= 16)
            {
                const auto* meminfo = reinterpret_cast<const uint32_t*>(reinterpret_cast<uint64_t>(tag) + 8);
                memory_kib = static_cast<uint64_t>(meminfo[0]) + static_cast<uint64_t>(meminfo[1]);
            }

            // Tags are padded to 8 bytes.
            offset += (tag->size + 7u) & ~static_cast<uint32_t>(7);
        }

        return memory_kib;
    }
}

extern "C" void kernel64_main(uint64_t multiboot_info_address)
{
    tinyos::drivers::serial::initialize();

    tinyos::drivers::serial::write_line("TinyOS x86_64 bootstrap");
    tinyos::drivers::serial::write_line(BootstrapVersion);
    tinyos::drivers::serial::write_line("Long mode entered.");

    tinyos::arch::x86_64::cpu::initialize();
    const auto& identity = tinyos::arch::x86_64::cpu::identity();
    tinyos::drivers::serial::write("CPU vendor: ");
    tinyos::drivers::serial::write_line(identity.vendor);
    tinyos::drivers::serial::write("Family/model/stepping: ");
    write_decimal(identity.family);
    tinyos::drivers::serial::write("/");
    write_decimal(identity.model);
    tinyos::drivers::serial::write("/");
    write_decimal(identity.stepping);
    tinyos::drivers::serial::write("Logical processors: ");
    write_decimal(identity.logical_processors);
    tinyos::drivers::serial::write_line("");

    tinyos::drivers::serial::write_line("CPU features:");
    write_feature("long-mode", identity.features.long_mode);
    write_feature("nx", identity.features.nx);
    write_feature("apic", identity.features.apic);
    write_feature("x2apic", identity.features.x2apic);
    write_feature("msr", identity.features.msr);
    write_feature("fxsave", identity.features.fxsave);
    write_feature("sse", identity.features.sse);
    write_feature("sse2", identity.features.sse2);
    write_feature("smep", identity.features.smep);
    write_feature("smap", identity.features.smap);
    write_feature("fsgsbase", identity.features.fsgsbase);
    write_feature("rdtsc", identity.features.rdtsc);
    write_feature("pdpe1gb", identity.features.pdpe1gb);
    write_feature("invpcid", identity.features.invpcid);
    write_feature("pcid", identity.features.pcid);
    write_feature("vmx", identity.features.vmx);
    write_feature("svm", identity.features.svm);

    if (!tinyos::arch::x86_64::cpu::validation_self_test())
    {
        tinyos::drivers::serial::write_line("FATAL: CPU capability probe failed.");
        asm volatile ("cli");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    tinyos::arch::x86_64::gdt::initialize();
    tinyos::drivers::serial::write_line("GDT and TSS loaded.");

    if (!tinyos::arch::x86_64::gdt::validation_self_test())
    {
        tinyos::drivers::serial::write_line("FATAL: GDT/TSS validation failed.");
        asm volatile ("cli");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    tinyos::arch::x86_64::interrupts::initialize();
    tinyos::drivers::serial::write_line("IDT loaded.");

    if (!tinyos::arch::x86_64::interrupts::validation_self_test())
    {
        tinyos::drivers::serial::write_line("FATAL: IDT validation failed.");
        asm volatile ("cli");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    const uint64_t memory_kib = scan_multiboot_info(multiboot_info_address);
    if (memory_kib == 0)
    {
        tinyos::drivers::serial::write_line("FATAL: Multiboot2 information block unusable.");
        asm volatile ("cli");
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    tinyos::drivers::serial::write("Usable memory: ");
    write_decimal(memory_kib / 1024);
    tinyos::drivers::serial::write_line(" MiB");

    tinyos::drivers::serial::write("Kernel image range: ");
    write_hex64(reinterpret_cast<uint64_t>(&kernel64_main));

    tinyos::drivers::serial::write_line("x86_64 long mode active.");
    tinyos::drivers::serial::write_line("x86_64 bootstrap complete.");
    tinyos::drivers::serial::write_line("TinyOS booted successfully.");

    // There is no scheduler or interrupt controller on this path yet. Halting
    // with interrupts disabled keeps the machine in a deterministic state that
    // the boot smoke test can observe instead of letting it spin on garbage.
    asm volatile ("cli");
    for (;;)
    {
        asm volatile ("hlt");
    }
}
