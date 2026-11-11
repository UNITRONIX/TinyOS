#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::paging
{
    inline constexpr uint32_t PageFlagRead = 1u << 0;
    inline constexpr uint32_t PageFlagWrite = 1u << 1;
    inline constexpr uint32_t PageFlagUser = 1u << 2;
    inline constexpr uint32_t PageFlagExecute = 1u << 3;

    // Upper bound for the identity map on the 32-bit kernel. The map is sized at
    // runtime from the detected memory, so this is a ceiling rather than the
    // configured size. The previous fixed 16 MiB value silently discarded RAM on
    // any machine with more memory than that.
    //
    // Declared as uint64_t: on the 32-bit kernel size_t is 32 bits wide, so a
    // 4 GiB ceiling expressed in size_t wraps to zero and every derived table
    // count silently becomes zero as well.
    inline constexpr uint64_t BootstrapIdentityLimit = 4ull * 1024ull * 1024ull * 1024ull;

    // Granularity the identity map grows in. Exposed because the address-space
    // bookkeeping is built before paging initialises and must predict the same
    // size the paging layer will map.
    inline constexpr size_t IdentityMapChunkBytes = 64ull * 1024ull * 1024ull;

    // Computes the identity-map size the paging layer will produce for a machine
    // with `detected_bytes` of physical memory. Kept in one place so the two
    // layers cannot disagree.
    size_t planned_identity_bytes(size_t detected_bytes);

    struct PageMapping
    {
        uintptr_t virtual_address;
        uintptr_t physical_address;
        uint32_t flags;
        bool present;
    };

    void initialize();
    bool is_ready();
    void enable_runtime();
    bool is_runtime_enabled();
    uintptr_t page_directory_address();
    uintptr_t active_page_directory_address();
    size_t bootstrap_identity_bytes();
    size_t mapped_pages();
    size_t mapped_bytes();
    uint32_t bootstrap_page_flags();
    bool mapping_for(uintptr_t virtual_address, PageMapping& mapping);
    bool update_mapping_flags(uintptr_t virtual_address, uint32_t flags);
    bool clear_page_present(uintptr_t virtual_address);
    size_t update_mapping_flags_for_range(uintptr_t virtual_base, size_t size, uint32_t flags);
    size_t map_identity_range(uintptr_t physical_base, size_t size, uint32_t flags);
    bool is_bootstrap_identity_mapped(uintptr_t virtual_address);
    bool validation_self_test();
}
