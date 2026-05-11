#include <stddef.h>
#include <stdint.h>

#include <tinyos/boot/multiboot.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/address_space.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/paging.hpp>

namespace
{
    constexpr size_t MaxRegions = 16;
    constexpr uintptr_t BootstrapIdentityLimit = 16 * 1024 * 1024;
    constexpr uint32_t KernelRegionFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite | tinyos::kernel::memory::paging::PageFlagExecute;
    constexpr uint32_t KernelMetadataFlags = tinyos::kernel::memory::paging::PageFlagRead;
    constexpr uint32_t KernelTextFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagExecute;
    constexpr uint32_t KernelReadOnlyDataFlags = tinyos::kernel::memory::paging::PageFlagRead;
    constexpr uint32_t KernelWritableDataFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite;
    constexpr uint32_t BootModuleRegionFlags = tinyos::kernel::memory::paging::PageFlagRead;
    constexpr uint32_t SupportedRegionFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite | tinyos::kernel::memory::paging::PageFlagUser | tinyos::kernel::memory::paging::PageFlagExecute;

    tinyos::kernel::memory::address_space::Region g_regions[MaxRegions] = {};
    size_t g_region_count = 0;
    size_t g_kernel_section_region_count = 0;
    size_t g_boot_module_region_count = 0;
    size_t g_rejected_region_count = 0;
    size_t g_total_mapped_bytes = 0;
    bool g_ready = false;

    extern "C" char __kernel_start;
    extern "C" char __kernel_end;
    extern "C" char __kernel_text_start;
    extern "C" char __kernel_text_end;
    extern "C" char __kernel_rodata_start;
    extern "C" char __kernel_rodata_end;
    extern "C" char __kernel_data_start;
    extern "C" char __kernel_bss_end;

    uintptr_t align_down(uintptr_t value, uintptr_t alignment)
    {
        return value & ~(alignment - 1);
    }

    uintptr_t align_up(uintptr_t value, uintptr_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    bool is_aligned(uintptr_t value)
    {
        return (value % tinyos::kernel::memory::frames::FrameSize) == 0;
    }

    bool flags_are_valid(uint32_t flags)
    {
        if ((flags & ~SupportedRegionFlags) != 0)
        {
            return false;
        }

        if ((flags & tinyos::kernel::memory::paging::PageFlagRead) == 0)
        {
            return false;
        }

        return (flags & tinyos::kernel::memory::paging::PageFlagUser) == 0;
    }

    bool is_kernel_section_type(tinyos::kernel::memory::address_space::RegionType type)
    {
        return type == tinyos::kernel::memory::address_space::RegionType::KernelMetadata
            || type == tinyos::kernel::memory::address_space::RegionType::KernelText
            || type == tinyos::kernel::memory::address_space::RegionType::KernelReadOnlyData
            || type == tinyos::kernel::memory::address_space::RegionType::KernelWritableData;
    }

    bool region_is_identity_backed(const tinyos::kernel::memory::address_space::Region& region)
    {
        return region.virtual_base == region.physical_base;
    }

    bool paging_mapping_matches_region(const tinyos::kernel::memory::address_space::Region& region, uintptr_t address)
    {
        constexpr uint32_t EnforceableFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite | tinyos::kernel::memory::paging::PageFlagUser;
        tinyos::kernel::memory::paging::PageMapping mapping;
        if (!tinyos::kernel::memory::paging::mapping_for(address, mapping))
        {
            return false;
        }

        if (!mapping.present || mapping.physical_address != address)
        {
            return false;
        }

        const uint32_t expected_flags = region.flags & EnforceableFlags;
        const uint32_t actual_flags = mapping.flags & EnforceableFlags;
        if ((actual_flags & expected_flags) != expected_flags)
        {
            return false;
        }

        const uint32_t extra_flags = actual_flags & ~expected_flags;
        return extra_flags == 0;
    }

    bool region_is_valid(const tinyos::kernel::memory::address_space::Region& region)
    {
        if (region.name == nullptr || region.size == 0 || !is_aligned(region.virtual_base) || !is_aligned(region.physical_base) || !is_aligned(region.size))
        {
            return false;
        }

        if (!flags_are_valid(region.flags))
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::IdentityMapped && !region_is_identity_backed(region))
        {
            return false;
        }

        if ((region.type == tinyos::kernel::memory::address_space::RegionType::KernelImage || is_kernel_section_type(region.type)) && !region_is_identity_backed(region))
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::KernelMetadata && region.flags != KernelMetadataFlags)
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::KernelText && region.flags != KernelTextFlags)
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::KernelReadOnlyData && region.flags != KernelReadOnlyDataFlags)
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::KernelWritableData && region.flags != KernelWritableDataFlags)
        {
            return false;
        }

        if (region.type == tinyos::kernel::memory::address_space::RegionType::BootModule && region.flags != BootModuleRegionFlags)
        {
            return false;
        }

        return true;
    }

    bool add_region(const char* name, uintptr_t virtual_base, uintptr_t physical_base, size_t size, uint32_t flags, tinyos::kernel::memory::address_space::RegionType type)
    {
        if (g_region_count >= MaxRegions || size == 0)
        {
            ++g_rejected_region_count;
            return false;
        }

        g_regions[g_region_count].name = name;
        g_regions[g_region_count].virtual_base = virtual_base;
        g_regions[g_region_count].physical_base = physical_base;
        g_regions[g_region_count].size = size;
        g_regions[g_region_count].flags = flags;
        g_regions[g_region_count].type = type;
        g_total_mapped_bytes += size;
        ++g_region_count;
        if (is_kernel_section_type(type))
        {
            ++g_kernel_section_region_count;
        }

        if (type == tinyos::kernel::memory::address_space::RegionType::BootModule)
        {
            ++g_boot_module_region_count;
        }

        return true;
    }

    void add_kernel_section_region(const char* name, uintptr_t start, uintptr_t end, uint32_t flags, tinyos::kernel::memory::address_space::RegionType type)
    {
        const uintptr_t aligned_start = align_down(start, tinyos::kernel::memory::frames::FrameSize);
        const uintptr_t aligned_end = align_up(end, tinyos::kernel::memory::frames::FrameSize);
        if (aligned_end <= aligned_start)
        {
            ++g_rejected_region_count;
            return;
        }

        add_region(name, aligned_start, aligned_start, aligned_end - aligned_start, flags, type);
    }

    void add_kernel_image_regions()
    {
        add_kernel_section_region(
            "kernel-metadata",
            reinterpret_cast<uintptr_t>(&__kernel_start),
            reinterpret_cast<uintptr_t>(&__kernel_text_start),
            KernelMetadataFlags,
            tinyos::kernel::memory::address_space::RegionType::KernelMetadata);

        add_kernel_section_region(
            "kernel-text",
            reinterpret_cast<uintptr_t>(&__kernel_text_start),
            reinterpret_cast<uintptr_t>(&__kernel_text_end),
            KernelTextFlags,
            tinyos::kernel::memory::address_space::RegionType::KernelText);

        add_kernel_section_region(
            "kernel-rodata",
            reinterpret_cast<uintptr_t>(&__kernel_rodata_start),
            reinterpret_cast<uintptr_t>(&__kernel_rodata_end),
            KernelReadOnlyDataFlags,
            tinyos::kernel::memory::address_space::RegionType::KernelReadOnlyData);

        add_kernel_section_region(
            "kernel-data",
            reinterpret_cast<uintptr_t>(&__kernel_data_start),
            reinterpret_cast<uintptr_t>(&__kernel_bss_end),
            KernelWritableDataFlags,
            tinyos::kernel::memory::address_space::RegionType::KernelWritableData);
    }

    void add_boot_module_regions(uint32_t multiboot_info_addr)
    {
        const auto* info = reinterpret_cast<const tinyos::boot::multiboot::Info*>(multiboot_info_addr);
        if ((info->flags & tinyos::boot::multiboot::FlagModules) == 0 || info->mods_count == 0 || info->mods_addr == 0)
        {
            return;
        }

        const auto* modules = reinterpret_cast<const tinyos::boot::multiboot::ModuleEntry*>(info->mods_addr);
        for (uint32_t index = 0; index < info->mods_count; ++index)
        {
            const auto& module = modules[index];
            if (module.mod_start == 0 || module.mod_end <= module.mod_start)
            {
                ++g_rejected_region_count;
                continue;
            }

            const uintptr_t module_start = align_down(module.mod_start, tinyos::kernel::memory::frames::FrameSize);
            const uintptr_t module_end = align_up(module.mod_end, tinyos::kernel::memory::frames::FrameSize);
            if (module_end <= module_start)
            {
                ++g_rejected_region_count;
                continue;
            }

            add_region(
                "boot-module",
                module_start,
                module_start,
                module_end - module_start,
                BootModuleRegionFlags,
                tinyos::kernel::memory::address_space::RegionType::BootModule);
        }
    }
}

namespace tinyos::kernel::memory::address_space
{
    void initialize(uint32_t multiboot_info_addr)
    {
        g_region_count = 0;
        g_kernel_section_region_count = 0;
        g_boot_module_region_count = 0;
        g_rejected_region_count = 0;
        g_total_mapped_bytes = 0;
        g_ready = false;

        add_region(
            "identity-lowmem",
            0,
            0,
            BootstrapIdentityLimit,
            KernelRegionFlags,
            RegionType::IdentityMapped);

        add_kernel_image_regions();

        add_boot_module_regions(multiboot_info_addr);

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

    size_t kernel_section_region_count()
    {
        return g_kernel_section_region_count;
    }

    size_t boot_module_region_count()
    {
        return g_boot_module_region_count;
    }

    size_t rejected_region_count()
    {
        return g_rejected_region_count;
    }

    const Region* region_at(size_t index)
    {
        if (index >= g_region_count)
        {
            return nullptr;
        }

        return &g_regions[index];
    }

    const char* region_type_name(RegionType type)
    {
        switch (type)
        {
        case RegionType::IdentityMapped:
            return "identity";
        case RegionType::KernelImage:
            return "kernel";
        case RegionType::KernelMetadata:
            return "kernel-metadata";
        case RegionType::KernelText:
            return "kernel-text";
        case RegionType::KernelReadOnlyData:
            return "kernel-rodata";
        case RegionType::KernelWritableData:
            return "kernel-data";
        case RegionType::BootModule:
            return "boot-module";
        }

        return "unknown";
    }

    size_t total_mapped_bytes()
    {
        return g_total_mapped_bytes;
    }

    size_t apply_paging_policy()
    {
        if (!g_ready || !paging::is_ready())
        {
            return 0;
        }

        size_t updated_pages = 0;
        for (size_t index = 0; index < g_region_count; ++index)
        {
            const Region& region = g_regions[index];
            if (region.type == RegionType::IdentityMapped)
            {
                continue;
            }

            updated_pages += paging::update_mapping_flags_for_range(region.virtual_base, region.size, region.flags);
        }

        return updated_pages;
    }

    size_t paging_policy_gap_count()
    {
        if (!g_ready || !paging::is_ready())
        {
            return 0;
        }

        size_t gap_count = 0;
        for (size_t region_index = 0; region_index < g_region_count; ++region_index)
        {
            const Region& region = g_regions[region_index];
            if (region.type == RegionType::IdentityMapped)
            {
                continue;
            }

            for (uintptr_t offset = 0; offset < region.size; offset += frames::FrameSize)
            {
                const uintptr_t address = region.virtual_base + offset;
                if (!paging_mapping_matches_region(region, address))
                {
                    ++gap_count;
                }
            }
        }

        return gap_count;
    }

    bool validation_self_test()
    {
        if (!g_ready || g_region_count < 2 || g_region_count > MaxRegions || g_total_mapped_bytes == 0)
        {
            return false;
        }

        bool has_identity_region = false;
        bool has_kernel_metadata_region = false;
        bool has_kernel_text_region = false;
        bool has_kernel_rodata_region = false;
        bool has_kernel_data_region = false;
        size_t observed_total = 0;
        size_t observed_kernel_sections = 0;
        size_t observed_boot_modules = 0;
        for (size_t index = 0; index < g_region_count; ++index)
        {
            const Region& region = g_regions[index];
            if (!region_is_valid(region))
            {
                return false;
            }

            observed_total += region.size;
            if (region.type == RegionType::IdentityMapped && region.virtual_base == 0 && region.size >= BootstrapIdentityLimit)
            {
                has_identity_region = true;
            }

            if (is_kernel_section_type(region.type))
            {
                ++observed_kernel_sections;
            }

            if (region.type == RegionType::KernelMetadata)
            {
                has_kernel_metadata_region = true;
            }

            if (region.type == RegionType::KernelText)
            {
                has_kernel_text_region = true;
            }

            if (region.type == RegionType::KernelReadOnlyData)
            {
                has_kernel_rodata_region = true;
            }

            if (region.type == RegionType::KernelWritableData)
            {
                has_kernel_data_region = true;
            }

            if (region.type == RegionType::BootModule)
            {
                ++observed_boot_modules;
            }
        }

        return has_identity_region
            && has_kernel_metadata_region
            && has_kernel_text_region
            && has_kernel_rodata_region
            && has_kernel_data_region
            && observed_total == g_total_mapped_bytes
            && observed_kernel_sections == g_kernel_section_region_count
            && observed_boot_modules == g_boot_module_region_count
            && g_rejected_region_count == 0;
    }
}
