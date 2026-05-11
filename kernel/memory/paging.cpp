#include <stdint.h>

#include <tinyos/arch/hal.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>

namespace
{
    constexpr size_t EntriesPerTable = 1024;
    constexpr size_t BootstrapPageTables = 16;
    constexpr size_t PageOffsetMask = tinyos::kernel::memory::frames::FrameSize - 1;
    constexpr uint32_t EntryAddressMask = 0xFFFFF000;
    constexpr uint32_t PagePresent = 0x001;
    constexpr uint32_t PageWritable = 0x002;
    constexpr uint32_t PageUser = 0x004;
    constexpr uint32_t BootstrapFlags = tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite | tinyos::kernel::memory::paging::PageFlagExecute;

    alignas(4096) uint32_t* g_page_directory = nullptr;
    size_t g_mapped_pages = 0;
    bool g_ready = false;
    bool g_runtime_enabled = false;

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

        const uint32_t bootstrap_entry_bits = flags_to_entry_bits(BootstrapFlags);
        for (size_t directory_index = 0; directory_index < BootstrapPageTables; ++directory_index)
        {
            const uintptr_t table_address = frames::allocate_pages(1);
            if (table_address == 0)
            {
                kernel::panic("Failed to allocate page table.");
            }

            auto* table = reinterpret_cast<uint32_t*>(table_address);
            zero_page(table);

            for (size_t entry_index = 0; entry_index < EntriesPerTable; ++entry_index)
            {
                const uintptr_t physical = ((directory_index * EntriesPerTable) + entry_index) * frames::FrameSize;
                table[entry_index] = static_cast<uint32_t>(physical) | bootstrap_entry_bits;
                ++g_mapped_pages;
            }

            g_page_directory[directory_index] = static_cast<uint32_t>(table_address) | bootstrap_entry_bits;
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
        return BootstrapPageTables * EntriesPerTable * frames::FrameSize;
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

        const uint32_t directory_entry = g_page_directory[directory_index];
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

        table[table_index] = (table_entry & EntryAddressMask) | flags_to_entry_bits(flags);
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

        return updated_pages;
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
