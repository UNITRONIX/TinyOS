#include <tinyos/boot/multiboot.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>

namespace
{
    constexpr size_t MaxRegions = 32;
    constexpr uint32_t UsableRegionType = 1;

    tinyos::kernel::memory::map::Region g_regions[MaxRegions] = {};
    size_t g_region_count = 0;
    uint64_t g_total_bytes = 0;
    uint64_t g_usable_bytes = 0;
}

namespace tinyos::kernel::memory::map
{
    void initialize(uint32_t multiboot_info_addr)
    {
        g_region_count = 0;
        g_total_bytes = 0;
        g_usable_bytes = 0;

        const auto* info = reinterpret_cast<const boot::multiboot::Info*>(multiboot_info_addr);
        if ((info->flags & boot::multiboot::FlagMemoryMap) == 0)
        {
            kernel::klog::write_line(kernel::klog::Level::Warn, "Multiboot memory map is unavailable.");
            return;
        }

        const uintptr_t mmap_begin = static_cast<uintptr_t>(info->mmap_addr);
        const uintptr_t mmap_end = mmap_begin + static_cast<uintptr_t>(info->mmap_length);

        for (uintptr_t current = mmap_begin; current < mmap_end;)
        {
            const auto* entry = reinterpret_cast<const boot::multiboot::MemoryMapEntry*>(current);

            g_total_bytes += entry->length;
            if (entry->type == UsableRegionType)
            {
                g_usable_bytes += entry->length;
            }

            if (g_region_count < MaxRegions)
            {
                g_regions[g_region_count].base = entry->base_addr;
                g_regions[g_region_count].length = entry->length;
                g_regions[g_region_count].type = entry->type;
                ++g_region_count;
            }

            current += static_cast<uintptr_t>(entry->size) + sizeof(entry->size);
        }

        kernel::klog::write_line(kernel::klog::Level::Info, "Multiboot memory map parsed.");
    }

    size_t region_count()
    {
        return g_region_count;
    }

    const Region& region(size_t index)
    {
        return g_regions[index];
    }

    uint64_t total_bytes()
    {
        return g_total_bytes;
    }

    uint64_t usable_bytes()
    {
        return g_usable_bytes;
    }
}
