#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::address_space
{
    enum class RegionType
    {
        IdentityMapped,
        KernelImage,
        KernelMetadata,
        KernelText,
        KernelReadOnlyData,
        KernelWritableData,
        BootModule
    };

    struct Region
    {
        const char* name;
        uintptr_t virtual_base;
        uintptr_t physical_base;
        size_t size;
        uint32_t flags;
        RegionType type;
    };

    void initialize(uint32_t multiboot_info_addr);
    bool is_ready();
    size_t region_count();
    size_t kernel_section_region_count();
    size_t boot_module_region_count();
    size_t rejected_region_count();
    const Region* region_at(size_t index);
    const char* region_type_name(RegionType type);
    size_t total_mapped_bytes();
    size_t apply_paging_policy();
    size_t paging_policy_gap_count();
    bool validation_self_test();
}
