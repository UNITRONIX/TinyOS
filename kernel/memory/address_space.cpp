#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/address_space.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>

namespace
{
    constexpr size_t MaxRegions = 8;
    constexpr uintptr_t BootstrapIdentityLimit = 16 * 1024 * 1024;
    constexpr uint32_t FlagPresent = 0x001;
    constexpr uint32_t FlagWritable = 0x002;

    tinyos::kernel::memory::address_space::Region g_regions[MaxRegions] = {};
    size_t g_region_count = 0;
    size_t g_total_mapped_bytes = 0;
    bool g_ready = false;

    extern "C" char __kernel_start;
    extern "C" char __kernel_end;

    uintptr_t align_down(uintptr_t value, uintptr_t alignment)
    {
        return value & ~(alignment - 1);
    }

    uintptr_t align_up(uintptr_t value, uintptr_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void add_region(const char* name, uintptr_t virtual_base, uintptr_t physical_base, size_t size, uint32_t flags, tinyos::kernel::memory::address_space::RegionType type)
    {
        if (g_region_count >= MaxRegions || size == 0)
        {
            return;
        }

        g_regions[g_region_count].name = name;
        g_regions[g_region_count].virtual_base = virtual_base;
        g_regions[g_region_count].physical_base = physical_base;
        g_regions[g_region_count].size = size;
        g_regions[g_region_count].flags = flags;
        g_regions[g_region_count].type = type;
        g_total_mapped_bytes += size;
        ++g_region_count;
    }
}

namespace tinyos::kernel::memory::address_space
{
    void initialize(uint32_t multiboot_info_addr)
    {
        (void)multiboot_info_addr;
        g_region_count = 0;
        g_total_mapped_bytes = 0;
        g_ready = false;

        add_region(
            "identity-lowmem",
            0,
            0,
            BootstrapIdentityLimit,
            FlagPresent | FlagWritable,
            RegionType::IdentityMapped);

        const uintptr_t kernel_start = align_down(reinterpret_cast<uintptr_t>(&__kernel_start), frames::FrameSize);
        const uintptr_t kernel_end = align_up(reinterpret_cast<uintptr_t>(&__kernel_end), frames::FrameSize);
        add_region(
            "kernel-image",
            kernel_start,
            kernel_start,
            kernel_end - kernel_start,
            FlagPresent | FlagWritable,
            RegionType::KernelImage);

        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Address space scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t region_count()
    {
        return g_region_count;
    }

    const Region* region_at(size_t index)
    {
        if (index >= g_region_count)
        {
            return nullptr;
        }

        return &g_regions[index];
    }

    size_t total_mapped_bytes()
    {
        return g_total_mapped_bytes;
    }
}
