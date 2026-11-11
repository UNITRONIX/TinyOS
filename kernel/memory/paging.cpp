#include <stdint.h>

#include <tinyos/arch/hal.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>

namespace
{
    constexpr size_t EntriesPerTable = 1024;
    constexpr size_t PageOffsetMask = tinyos::kernel::memory::frames::FrameSize - 1;

    // Computed in 64-bit: the 4 GiB ceiling does not fit in a 32-bit size_t, and
    // expressing it in size_t wraps to zero before the division happens.
    constexpr uint64_t IdentityTableCeiling64 =
        tinyos::kernel::memory::paging::BootstrapIdentityLimit /
        (static_cast<uint64_t>(EntriesPerTable) * static_cast<uint64_t>(tinyos::kernel::memory::frames::FrameSize));
    constexpr size_t IdentityTableCeiling = static_cast<size_t>(IdentityTableCeiling64);
    static_assert(IdentityTableCeiling64 > 0 && IdentityTableCeiling64 <= 1024,
        "Bootstrap identity map must fit in one page directory.");
    constexpr uint32_t EntryAddressMask = 0xFFFFF000;
    constexpr uint32_t PagePresent = 0x001;
    constexpr uint32_t PageWritable = 0x002;
    constexpr uint32_t PageUser = 0x004;
    constexpr uint32_t BootstrapFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite | tinyos::kernel::memory::paging::PageFlagExecute;

    alignas(4096) uint32_t* g_page_directory = nullptr;
    size_t g_mapped_pages = 0;
    size_t g_identity_table_count = 0;
    bool g_ready = false;
    bool g_runtime_enabled = false;

    // Chunk of RAM the bootstrap identity map is grown by at a time. Mapping the
    // whole ceiling up front costs (ceiling / 4 MiB) page tables and their
    // pages, which is wasteful on the small targets TinyOS also runs on.
    constexpr size_t IdentityChunkBytes = tinyos::kernel::memory::paging::IdentityMapChunkBytes;

    // One page table maps 4 MiB (1024 entries x 4096 bytes).
    constexpr size_t BytesPerTable = EntriesPerTable * tinyos::kernel::memory::frames::FrameSize;

    size_t bytes_to_tables(size_t bytes)
    {
        const size_t tables = (bytes + BytesPerTable - 1) / BytesPerTable;
        return tables > IdentityTableCeiling ? IdentityTableCeiling : tables;
    }

    void zero_page(uint32_t* page)
    {
        for (size_t index = 0; index < tinyos::kernel::memory::frames::FrameSize / sizeof(uint32_t); ++index)
        {
            page[index] = 0;
        }
    }

    uint32_t flags_to_entry_bits(uint32_t flags)
    {
        uint32_t entry = 0;
        if ((flags & tinyos::kernel::memory::paging::PageFlagRead) != 0)
        {
            entry |= PagePresent;
        }

        if ((flags & tinyos::kernel::memory::paging::PageFlagWrite) != 0)
        {
            entry |= PageWritable;
        }

        if ((flags & tinyos::kernel::memory::paging::PageFlagUser) != 0)
        {
            entry |= PageUser;
        }

        return entry;
    }

    uint32_t entry_bits_to_flags(uint32_t entry)
    {
        if ((entry & PagePresent) == 0)
        {
            return 0;
        }

        uint32_t flags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagExecute;
        if ((entry & PageWritable) != 0)
        {
            flags |= tinyos::kernel::memory::paging::PageFlagWrite;
        }

        if ((entry & PageUser) != 0)
        {
            flags |= tinyos::kernel::memory::paging::PageFlagUser;
        }

        return flags;
    }
}

namespace tinyos::kernel::memory::paging
{
    size_t planned_identity_bytes(size_t detected_bytes)
    {
        // Round up only to the next page-table granularity (4 MiB), never to a
        // large fixed chunk: mapping memory the machine does not have wastes page
        // tables and pushes the boot path over its frame budget on small
        // machines.
        //
        // All arithmetic is 64-bit because the 4 GiB ceiling does not fit in a
        // 32-bit size_t.
        constexpr uint64_t Granularity = 4ull * 1024ull * 1024ull;

        uint64_t desired = detected_bytes != 0 ? static_cast<uint64_t>(detected_bytes) : Granularity;
        desired = ((desired + Granularity - 1) / Granularity) * Granularity;
        if (desired > BootstrapIdentityLimit)
        {
            desired = BootstrapIdentityLimit;
        }

        return static_cast<size_t>(desired);
    }

    namespace
    {
        // Maps more physical memory into the identity window. Returns the number
        // of pages added. Growing on demand keeps boot memory proportional to the
        // RAM actually present instead of to the ceiling.
        //
        // Note: this must not test g_ready. It runs during initialize(), before
        // g_ready is set, so checking it here made the very first call a no-op
        // that reported zero pages.
        size_t grow_identity_map(size_t additional_tables)
        {
            if (g_page_directory == nullptr || additional_tables == 0)
            {
                return 0;
            }

            const size_t target_tables = g_identity_table_count + additional_tables > IdentityTableCeiling
                ? IdentityTableCeiling
                : g_identity_table_count + additional_tables;

            const uint32_t bootstrap_entry_bits = flags_to_entry_bits(BootstrapFlags);
            size_t added_pages = 0;

            for (size_t directory_index = g_identity_table_count; directory_index < target_tables; ++directory_index)
            {
                const uintptr_t table_address = frames::allocate_pages(1);
                if (table_address == 0)
                {
                    break;
                }

                auto* table = reinterpret_cast<uint32_t*>(table_address);
                zero_page(table);

                for (size_t entry_index = 0; entry_index < EntriesPerTable; ++entry_index)
                {
                    const uintptr_t physical = ((directory_index * EntriesPerTable) + entry_index) * frames::FrameSize;
                    table[entry_index] = static_cast<uint32_t>(physical) | bootstrap_entry_bits;
                    ++g_mapped_pages;
                    ++added_pages;
                }

                g_page_directory[directory_index] = static_cast<uint32_t>(table_address) | bootstrap_entry_bits;
                ++g_identity_table_count;
            }

            if (added_pages != 0 && g_runtime_enabled)
            {
                arch::load_page_directory(page_directory_address());
            }

            return added_pages;
        }
    }

    void initialize()
    {
        if (g_ready)
        {
            return;
        }

        const uintptr_t directory_address = frames::allocate_pages(1);
        if (directory_address == 0)
        {
            kernel::panic("Failed to allocate page directory.");
        }

        g_page_directory = reinterpret_cast<uint32_t*>(directory_address);
        zero_page(g_page_directory);
        g_identity_table_count = 0;
        g_mapped_pages = 0;

        // Cover the memory the machine actually has, rounded up to the chunk
        // size, instead of a fixed 16 MiB window.
        const size_t detected_bytes = static_cast<size_t>(map::total_bytes());
        const size_t desired_bytes = planned_identity_bytes(detected_bytes);

        const size_t tables = bytes_to_tables(desired_bytes);
        if (grow_identity_map(tables) == 0)
        {
            kernel::panic("Failed to build the bootstrap identity map.");
        }

        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Paging structures prepared.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    void enable_runtime()
    {
        if (!g_ready || g_page_directory == nullptr)
        {
            kernel::panic("Cannot enable paging before paging structures are ready.");
        }

        if (g_runtime_enabled)
        {
            return;
        }

        arch::load_page_directory(page_directory_address());
        arch::enable_paging();
        g_runtime_enabled = arch::paging_enabled() && arch::active_page_directory() == page_directory_address();
        if (!g_runtime_enabled)
        {
            kernel::panic("Failed to enable runtime paging.");
        }

        kernel::klog::write_line(kernel::klog::Level::Info, "Runtime paging enabled.");
    }

    bool is_runtime_enabled()
    {
        return g_runtime_enabled && arch::paging_enabled();
    }

    uintptr_t page_directory_address()
    {
        return reinterpret_cast<uintptr_t>(g_page_directory);
    }

    uintptr_t active_page_directory_address()
    {
        return arch::active_page_directory();
    }

    size_t bootstrap_identity_bytes()
    {
        return g_identity_table_count * BytesPerTable;
    }

    size_t mapped_pages()
    {
        return g_mapped_pages;
    }

    size_t mapped_bytes()
    {
        return g_mapped_pages * frames::FrameSize;
    }

    uint32_t bootstrap_page_flags()
    {
        return BootstrapFlags;
    }

    bool mapping_for(uintptr_t virtual_address, PageMapping& mapping)
    {
        mapping.virtual_address = virtual_address & ~static_cast<uintptr_t>(PageOffsetMask);
        mapping.physical_address = 0;
        mapping.flags = 0;
        mapping.present = false;

        if (!g_ready || g_page_directory == nullptr)
        {
            return false;
        }

        const size_t directory_index = static_cast<size_t>(virtual_address >> 22);
        const size_t table_index = static_cast<size_t>((virtual_address >> 12) & 0x3FF);
        if (directory_index >= EntriesPerTable)
        {
            return false;
        }

        const uint32_t directory_entry = g_page_directory[directory_index];
        if ((directory_entry & PagePresent) == 0)
        {
            return false;
        }

        const auto* table = reinterpret_cast<const uint32_t*>(directory_entry & EntryAddressMask);
        const uint32_t table_entry = table[table_index];
        if ((table_entry & PagePresent) == 0)
        {
            return false;
        }

        mapping.physical_address = (table_entry & EntryAddressMask) | (virtual_address & PageOffsetMask);
        mapping.flags = entry_bits_to_flags(table_entry);
        mapping.present = true;
        return true;
    }

    bool update_mapping_flags(uintptr_t virtual_address, uint32_t flags)
    {
        if (!g_ready || g_page_directory == nullptr || (flags & PageFlagRead) == 0)
        {
            return false;
        }

        const size_t directory_index = static_cast<size_t>(virtual_address >> 22);
        const size_t table_index = static_cast<size_t>((virtual_address >> 12) & 0x3FF);
        if (directory_index >= EntriesPerTable)
        {
            return false;
        }

        uint32_t directory_entry = g_page_directory[directory_index];
        if ((directory_entry & PagePresent) == 0)
        {
            return false;
        }

        auto* table = reinterpret_cast<uint32_t*>(directory_entry & EntryAddressMask);
        const uint32_t table_entry = table[table_index];
        if ((table_entry & PagePresent) == 0)
        {
            return false;
        }

        const uint32_t entry_bits = flags_to_entry_bits(flags);
        table[table_index] = (table_entry & EntryAddressMask) | entry_bits;

        // User-mode walks require U=1 on both the PDE and the PTE.
        if ((flags & PageFlagUser) != 0)
        {
            g_page_directory[directory_index] = directory_entry | PageUser;
        }

        return true;
    }

    bool clear_page_present(uintptr_t virtual_address)
    {
        if (!g_ready || g_page_directory == nullptr)
        {
            return false;
        }

        const size_t directory_index = static_cast<size_t>(virtual_address >> 22);
        const size_t table_index = static_cast<size_t>((virtual_address >> 12) & 0x3FF);
        if (directory_index >= EntriesPerTable)
        {
            return false;
        }

        const uint32_t directory_entry = g_page_directory[directory_index];
        if ((directory_entry & PagePresent) == 0)
        {
            return false;
        }

        auto* table = reinterpret_cast<uint32_t*>(directory_entry & EntryAddressMask);
        const uint32_t table_entry = table[table_index];
        if ((table_entry & PagePresent) == 0)
        {
            return true;
        }

        table[table_index] = table_entry & EntryAddressMask;
        if (g_runtime_enabled)
        {
            arch::load_page_directory(page_directory_address());
        }

        return true;
    }

    size_t update_mapping_flags_for_range(uintptr_t virtual_base, size_t size, uint32_t flags)
    {
        if (size == 0)
        {
            return 0;
        }

        size_t updated_pages = 0;
        const uintptr_t aligned_base = virtual_base & ~static_cast<uintptr_t>(PageOffsetMask);
        const uintptr_t aligned_end = (virtual_base + size + PageOffsetMask) & ~static_cast<uintptr_t>(PageOffsetMask);
        for (uintptr_t address = aligned_base; address < aligned_end; address += frames::FrameSize)
        {
            if (update_mapping_flags(address, flags))
            {
                ++updated_pages;
            }
        }

        if (updated_pages != 0 && g_runtime_enabled)
        {
            arch::load_page_directory(page_directory_address());
        }

        return updated_pages;
    }

    size_t map_identity_range(uintptr_t physical_base, size_t size, uint32_t flags)
    {
        if (!g_ready || g_page_directory == nullptr || size == 0)
        {
            return 0;
        }

        const uint32_t effective_flags = (flags & PageFlagRead) != 0 ? flags : (PageFlagRead | PageFlagWrite);
        const uint32_t entry_bits = flags_to_entry_bits(effective_flags);
        const uintptr_t aligned_base = physical_base & ~static_cast<uintptr_t>(PageOffsetMask);
        const uintptr_t aligned_end = (physical_base + size + PageOffsetMask) & ~static_cast<uintptr_t>(PageOffsetMask);

        size_t mapped = 0;
        for (uintptr_t address = aligned_base; address < aligned_end; address += frames::FrameSize)
        {
            const size_t directory_index = static_cast<size_t>(address >> 22);
            const size_t table_index = static_cast<size_t>((address >> 12) & 0x3FF);
            if (directory_index >= EntriesPerTable)
            {
                break;
            }

            uint32_t directory_entry = g_page_directory[directory_index];
            uint32_t* table = nullptr;
            if ((directory_entry & PagePresent) == 0)
            {
                const uintptr_t table_address = frames::allocate_pages(1);
                if (table_address == 0)
                {
                    break;
                }

                table = reinterpret_cast<uint32_t*>(table_address);
                zero_page(table);
                g_page_directory[directory_index] = static_cast<uint32_t>(table_address) | entry_bits;
            }
            else
            {
                table = reinterpret_cast<uint32_t*>(directory_entry & EntryAddressMask);
            }

            const uint32_t previous = table[table_index];
            table[table_index] = static_cast<uint32_t>(address) | entry_bits;
            if ((previous & PagePresent) == 0)
            {
                ++g_mapped_pages;
            }
            ++mapped;
        }

        if (mapped != 0 && g_runtime_enabled)
        {
            arch::load_page_directory(page_directory_address());
        }

        return mapped;
    }

    bool is_bootstrap_identity_mapped(uintptr_t virtual_address)
    {
        PageMapping mapping;
        if (!mapping_for(virtual_address, mapping))
        {
            return false;
        }

        return mapping.present && mapping.physical_address == virtual_address && (mapping.flags & BootstrapFlags) == BootstrapFlags && (mapping.flags & PageFlagUser) == 0;
    }

    bool validation_self_test()
    {
        if (!g_ready || g_page_directory == nullptr || g_mapped_pages == 0)
        {
            return false;
        }

        if (mapped_bytes() != bootstrap_identity_bytes())
        {
            return false;
        }

        if (!is_bootstrap_identity_mapped(0) || !is_bootstrap_identity_mapped(frames::FrameSize) || !is_bootstrap_identity_mapped(bootstrap_identity_bytes() - frames::FrameSize))
        {
            return false;
        }

        PageMapping boundary_mapping;
        if (mapping_for(bootstrap_identity_bytes(), boundary_mapping))
        {
            return false;
        }

        return bootstrap_page_flags() == BootstrapFlags;
    }
}
