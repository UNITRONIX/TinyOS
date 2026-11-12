#include <tinyos/arch/gdt.hpp>
#include <tinyos/arch/hal.hpp>
#include <tinyos/arch/io.hpp>

#include <stddef.h>
#include <stdint.h>

namespace
{
    constexpr uint32_t Cr0Paging = 0x80000000;

    // CPUID leaf 1, ECX bit 30 is the RDRAND feature flag.
    constexpr uint32_t CpuidFeatureEcxRdrand = 1u << 30;

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

    bool g_hardware_entropy = false;
    uint32_t g_entropy_counter = 0;

    uint32_t read_cr0()
    {
        uint32_t value = 0;
        asm volatile ("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    void cpuid(uint32_t leaf, uint32_t& eax, uint32_t& ebx, uint32_t& ecx, uint32_t& edx)
    {
        // EBX is reserved by the compiler for PIC if that were enabled; the
        // kernel is built -fno-pic, but pushing and restoring it keeps the
        // constraint list honest either way.
        asm volatile (
            "push %%ebx\n"
            "cpuid\n"
            "pop %%ebx\n"
            : "=a"(eax), "=c"(ecx), "=d"(edx)
            : "a"(leaf)
            : "memory");
        (void)ebx;
    }

    // Reads one 32-bit hardware random value. Returns false when the CPU does not
    // implement RDRAND or the instruction reports a failure after the retry
    // count recommended by Intel (10 attempts).
    bool try_rdrand(uint32_t& value)
    {
        if (!g_hardware_entropy)
        {
            return false;
        }

        for (int attempt = 0; attempt < 10; ++attempt)
        {
            unsigned char ok = 0;
            uint32_t result = 0;
            asm volatile (
                "rdrand %0\n"
                "setc %1\n"
                : "=r"(result), "=qm"(ok)
                : 
                : "memory");

            if (ok != 0)
            {
                value = result;
                return true;
            }
        }

        return false;
    }

    // Software fallback. This is NOT cryptographically strong: it mixes the
    // timestamp counter with a counter and a simple xorshift. It exists so boot
    // does not fail on CPUs without RDRAND, and has_hardware_entropy() lets
    // callers report the weakness rather than hide it.
    uint32_t software_random()
    {
        uint64_t counter = 0;
        asm volatile ("rdtsc" : "=A"(counter));

        ++g_entropy_counter;

        uint32_t value = static_cast<uint32_t>(counter) ^
            static_cast<uint32_t>(counter >> 32) ^
            g_entropy_counter;

        // xorshift32, iterated a few times to spread the low bits that rdtsc
        // provides.
        for (int round = 0; round < 4; ++round)
        {
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
        }

        return value;
    }
}

namespace tinyos::arch
{
    void initialize()
    {
        tinyos::arch::gdt::initialize();

        // Probe RDRAND before anything can ask for a salt. Doing it here means
        // the answer is available from the first call to random_bytes().
        uint32_t eax = 0;
        uint32_t ebx = 0;
        uint32_t ecx = 0;
        uint32_t edx = 0;
        cpuid(1, eax, ebx, ecx, edx);
        g_hardware_entropy = (ecx & CpuidFeatureEcxRdrand) != 0;
        g_entropy_counter = eax ^ edx;
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

    bool has_hardware_entropy()
    {
        return g_hardware_entropy;
    }

    void random_bytes(void* buffer, size_t length)
    {
        if (buffer == nullptr || length == 0)
        {
            return;
        }

        auto* bytes = static_cast<uint8_t*>(buffer);
        size_t filled = 0;

        while (filled < length)
        {
            uint32_t value = 0;
            if (!try_rdrand(value))
            {
                value = software_random();
            }

            const size_t take = (length - filled) < sizeof(value) ? (length - filled) : sizeof(value);
            for (size_t index = 0; index < take; ++index)
            {
                bytes[filled + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xFFu);
            }

            filled += take;
        }
    }

    uint64_t timestamp_counter()
    {
        uint64_t value = 0;
        asm volatile ("rdtsc" : "=A"(value));
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
