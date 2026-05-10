#pragma once

#include <stdint.h>

namespace tinyos::boot::multiboot
{
    inline constexpr uint32_t BootloaderMagic = 0x2BADB002;
    inline constexpr uint32_t FlagModules = 1u << 3;
    inline constexpr uint32_t FlagMemoryMap = 1u << 6;

    struct [[gnu::packed]] Info
    {
        uint32_t flags;
        uint32_t mem_lower;
        uint32_t mem_upper;
        uint32_t boot_device;
        uint32_t cmdline;
        uint32_t mods_count;
        uint32_t mods_addr;
        uint32_t syms[4];
        uint32_t mmap_length;
        uint32_t mmap_addr;
    };

    struct [[gnu::packed]] MemoryMapEntry
    {
        uint32_t size;
        uint64_t base_addr;
        uint64_t length;
        uint32_t type;
    };

    struct [[gnu::packed]] ModuleEntry
    {
        uint32_t mod_start;
        uint32_t mod_end;
        uint32_t string;
        uint32_t reserved;
    };
}
