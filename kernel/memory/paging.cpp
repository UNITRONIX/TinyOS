#include <stdint.h>

#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>

namespace
{
    constexpr size_t EntriesPerTable = 1024;
    constexpr size_t BootstrapPageTables = 16;
    constexpr uint32_t PagePresent = 0x001;
    constexpr uint32_t PageWritable = 0x002;

    alignas(4096) uint32_t* g_page_directory = nullptr;
    size_t g_mapped_pages = 0;
    bool g_ready = false;

    void zero_page(uint32_t* page)
    {
        for (size_t index = 0; index < tinyos::kernel::memory::frames::FrameSize / sizeof(uint32_t); ++index)
        {
            page[index] = 0;
        }
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
                table[entry_index] = static_cast<uint32_t>(physical) | PagePresent | PageWritable;
                ++g_mapped_pages;
            }

            g_page_directory[directory_index] = static_cast<uint32_t>(table_address) | PagePresent | PageWritable;
        }

        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Paging structures prepared.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    uintptr_t page_directory_address()
    {
        return reinterpret_cast<uintptr_t>(g_page_directory);
    }

    size_t mapped_pages()
    {
        return g_mapped_pages;
    }

    size_t mapped_bytes()
    {
        return g_mapped_pages * frames::FrameSize;
    }
}
