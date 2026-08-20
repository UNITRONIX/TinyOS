#include <tinyos/arch/gdt.hpp>
#include <tinyos/arch/hal.hpp>
#include <tinyos/arch/io.hpp>

#include <stdint.h>

namespace
{
    constexpr uint32_t Cr0Paging = 0x80000000;
    constexpr tinyos::arch::Info I686Info = {
        "i686",
        "x86",
        32,
        4096,
        true,
        true,
        false,
        true
    };

    uint32_t read_cr0()
    {
        uint32_t value = 0;
        asm volatile ("mov %%cr0, %0" : "=r"(value));
        return value;
    }
}

namespace tinyos::arch
{
    void initialize()
    {
        tinyos::arch::gdt::initialize();
    }

    const Info& info()
    {
        return I686Info;
    }

    bool validation_self_test()
    {
        return I686Info.name != nullptr
            && I686Info.cpu_family != nullptr
            && I686Info.pointer_bits == 32
            && I686Info.page_size == 4096
            && I686Info.protected_mode
            && I686Info.paging_supported
            && I686Info.little_endian
            && tinyos::arch::gdt::validation_self_test();
    }

    void load_page_directory(uintptr_t page_directory_address)
    {
        asm volatile ("mov %0, %%cr3" :: "r"(page_directory_address) : "memory");
    }

    void enable_paging()
    {
        const uint32_t cr0 = read_cr0();
        asm volatile ("mov %0, %%cr0" :: "r"(cr0 | Cr0Paging) : "memory");
    }

    bool paging_enabled()
    {
        return (read_cr0() & Cr0Paging) != 0;
    }

    uintptr_t active_page_directory()
    {
        uintptr_t value = 0;
        asm volatile ("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    [[noreturn]] void halt()
    {
        for (;;)
        {
            asm volatile ("hlt");
        }
    }

    [[noreturn]] void reboot()
    {
        while ((io::inb(0x64) & 0x02) != 0)
        {
        }

        io::outb(0x64, 0xFE);
        halt();
    }
}
