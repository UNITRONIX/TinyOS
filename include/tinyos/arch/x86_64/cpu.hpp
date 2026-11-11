#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::arch::x86_64::cpu
{
    struct Features
    {
        bool long_mode;
        bool nx;
        bool apic;
        bool x2apic;
        bool msr;
        bool fxsave;
        bool sse;
        bool sse2;
        bool smep;
        bool smap;
        bool fsgsbase;
        bool rdtsc;
        bool vmx;
        bool svm;
        bool acpi;
        bool pdpe1gb;
        bool fsgsbase_supported;
        bool invpcid;
        bool pcid;
    };

    struct Identity
    {
        const char* vendor;
        uint32_t family;
        uint32_t model;
        uint32_t stepping;
        uint32_t logical_processors;
        Features features;
    };

    // Reads CPUID on the boot processor and caches the result. Must be called
    // once before any other accessor.
    void initialize();

    const Identity& identity();
    const Features& features();

    bool write_cr4_bit(uint64_t mask, bool enable);

    uint64_t read_msr(uint32_t msr);
    void write_msr(uint32_t msr, uint64_t value);

    // Enables x87/SSE state and sets OSFXSR/OSXMMEXCPT so FXSAVE/FXRSTOR work.
    void initialize_fpu();

    bool validation_self_test();
    const char* vendor_name();
}
