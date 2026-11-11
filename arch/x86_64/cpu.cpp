#include <tinyos/arch/x86_64/cpu.hpp>
#include <tinyos/drivers/serial.hpp>

namespace
{
    constexpr uint32_t MsrEfer = 0xC0000080;
    constexpr uint32_t EferNxe = 1u << 11;

    constexpr uint32_t Cr4Pae = 1u << 5;
    constexpr uint32_t Cr4OsFxsr = 1u << 9;
    constexpr uint32_t Cr4OsXmmExcpt = 1u << 10;
    constexpr uint32_t Cr4Fsgsbase = 1u << 16;
    constexpr uint32_t Cr4OsXsave = 1u << 18;
    constexpr uint32_t Cr4Smep = 1u << 20;
    constexpr uint32_t Cr4Smap = 1u << 21;

    tinyos::arch::x86_64::cpu::Identity g_identity = {};
    bool g_ready = false;

    struct CpuidResult
    {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;
    };

    CpuidResult cpuid(uint32_t leaf, uint32_t subleaf)
    {
        CpuidResult result = {};
        asm volatile (
            "cpuid"
            : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
            : "a"(leaf), "c"(subleaf));
        return result;
    }

    uint64_t read_cr4()
    {
        uint64_t value = 0;
        asm volatile ("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    void write_cr4(uint64_t value)
    {
        asm volatile ("mov %0, %%cr4" : : "r"(value) : "memory");
    }

    void fill_vendor(char (&buffer)[13], const CpuidResult& leaf0)
    {
        const uint32_t words[3] = { leaf0.ebx, leaf0.edx, leaf0.ecx };
        for (size_t index = 0; index < 3; ++index)
        {
            for (size_t byte = 0; byte < 4; ++byte)
            {
                buffer[index * 4 + byte] = static_cast<char>((words[index] >> (byte * 8)) & 0xFF);
            }
        }
        buffer[12] = '\0';
    }

    // CPUID feature bits are spread across several leaves and register halves.
    // Grouping them here keeps the identity probe readable and makes it obvious
    // which capability comes from which bit when a port misbehaves.
    void probe_features(tinyos::arch::x86_64::cpu::Features& features)
    {
        const CpuidResult leaf1 = cpuid(1, 0);

        features.msr = (leaf1.edx & (1u << 5)) != 0;
        features.rdtsc = (leaf1.edx & (1u << 4)) != 0;
        features.fxsave = (leaf1.edx & (1u << 24)) != 0;
        features.sse = (leaf1.edx & (1u << 25)) != 0;
        features.sse2 = (leaf1.edx & (1u << 26)) != 0;
        features.apic = (leaf1.edx & (1u << 9)) != 0;
        features.acpi = (leaf1.edx & (1u << 22)) != 0;
        features.vmx = (leaf1.ecx & (1u << 5)) != 0;
        features.x2apic = (leaf1.ecx & (1u << 21)) != 0;
        features.smap = (leaf1.ecx & (1u << 20)) != 0;
        features.pcid = (leaf1.ecx & (1u << 17)) != 0;

        const CpuidResult leaf7 = cpuid(7, 0);
        features.fsgsbase = (leaf7.ebx & (1u << 0)) != 0;
        features.smep = (leaf7.ebx & (1u << 7)) != 0;
        features.invpcid = (leaf7.ebx & (1u << 10)) != 0;

        // NX is reported as the high bit of EDX in the extended feature leaf.
        const CpuidResult ext1 = cpuid(0x80000001u, 0);
        features.nx = (ext1.edx & (1u << 20)) != 0;
        features.svm = (ext1.ecx & (1u << 2)) != 0;
        features.pdpe1gb = (ext1.edx & (1u << 26)) != 0;

        // Long mode is bit 29 of the same extended leaf.
        features.long_mode = (ext1.edx & (1u << 29)) != 0;
        features.fsgsbase_supported = features.fsgsbase;
    }
}

namespace tinyos::arch::x86_64::cpu
{
    void initialize()
    {
        CpuidResult leaf0 = cpuid(0, 0);

        static char vendor[13];
        fill_vendor(vendor, leaf0);
        g_identity.vendor = vendor;

        CpuidResult leaf1 = cpuid(1, 0);
        g_identity.stepping = leaf1.eax & 0x0F;
        g_identity.model = ((leaf1.eax >> 4) & 0x0F) | (((leaf1.eax >> 16) & 0x0F) << 4);
        g_identity.family = (leaf1.eax >> 8) & 0x0F;
        if (g_identity.family == 0x0F)
        {
            g_identity.family += (leaf1.eax >> 20) & 0xFF;
        }

        const uint32_t logical = (leaf1.ebx >> 16) & 0xFF;
        g_identity.logical_processors = logical != 0 ? logical : 1;

        probe_features(g_identity.features);

        // Turn on the protection bits the CPU actually implements, and refuse to
        // claim a capability the hardware does not have. Enabling SMEP/SMAP on a
        // CPU without them raises #GP at the wrmsr/cr4 write, which is a much
        // worse failure than an honest report.
        uint64_t cr4 = read_cr4();
        cr4 |= Cr4Pae;
        if (g_identity.features.fxsave)
        {
            cr4 |= Cr4OsFxsr | Cr4OsXmmExcpt;
        }
        if (g_identity.features.smep)
        {
            cr4 |= Cr4Smep;
        }
        if (g_identity.features.smap)
        {
            cr4 |= Cr4Smap;
        }
        if (g_identity.features.fsgsbase)
        {
            cr4 |= Cr4Fsgsbase;
        }
        write_cr4(cr4);

        if (g_identity.features.nx)
        {
            write_msr(MsrEfer, read_msr(MsrEfer) | EferNxe); // EFER.NXE
        }

        g_ready = true;
    }

    const Identity& identity()
    {
        return g_identity;
    }

    const Features& features()
    {
        return g_identity.features;
    }

    bool write_cr4_bit(uint64_t mask, bool enable)
    {
        if (!g_ready)
        {
            return false;
        }

        uint64_t cr4 = read_cr4();
        cr4 = enable ? (cr4 | mask) : (cr4 & ~mask);
        write_cr4(cr4);
        return true;
    }

    uint64_t read_msr(uint32_t msr)
    {
        uint32_t low = 0;
        uint32_t high = 0;
        asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    void write_msr(uint32_t msr, uint64_t value)
    {
        const uint32_t low = static_cast<uint32_t>(value & 0xFFFFFFFFu);
        const uint32_t high = static_cast<uint32_t>(value >> 32);
        asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
    }

    void initialize_fpu()
    {
        // SSE is required by the x86_64 ABI for floating point argument passing,
        // so this is not optional: a kernel that skips it corrupts userland
        // float state on the first context switch.
        asm volatile ("fninit");
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        // Anything running TinyOS in a VM or on a machine from the last decade
        // must report these. Failing here means the CPUID probe is broken.
        return g_identity.vendor != nullptr &&
            g_identity.vendor[0] != '\0' &&
            g_identity.features.long_mode &&
            g_identity.features.msr &&
            g_identity.logical_processors >= 1;
    }

    const char* vendor_name()
    {
        return g_identity.vendor != nullptr ? g_identity.vendor : "unknown";
    }
}
