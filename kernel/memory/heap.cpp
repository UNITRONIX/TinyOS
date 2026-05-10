#include <stdint.h>

#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/heap.hpp>
#include <tinyos/kernel/panic.hpp>

namespace
{
    struct BlockHeader
    {
        uint32_t magic;
        size_t size;
        bool is_free;
        BlockHeader* next;
    };

    constexpr uint32_t BlockMagic = 0x544F5348;
    constexpr size_t Alignment = 8;
    constexpr size_t DefaultGrowthPages = 4;
    constexpr size_t MinimumSplitBytes = 16;

    BlockHeader* g_head = nullptr;
    size_t g_total_bytes = 0;
    size_t g_allocation_count = 0;
    size_t g_free_count = 0;
    size_t g_invalid_free_count = 0;
    size_t g_double_free_count = 0;
    size_t g_corrupt_block_count = 0;
    bool g_initialized = false;

    size_t align_up(size_t value)
    {
        return (value + Alignment - 1) & ~(Alignment - 1);
    }

    size_t header_bytes()
    {
        return align_up(sizeof(BlockHeader));
    }

    bool block_header_valid(const BlockHeader* block)
    {
        return block != nullptr && block->magic == BlockMagic;
    }

    uintptr_t payload_address(const BlockHeader* block)
    {
        return reinterpret_cast<uintptr_t>(block) + header_bytes();
    }

    BlockHeader* find_block_for_payload(const void* pointer)
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);

        for (BlockHeader* block = g_head; block != nullptr; block = block->next)
        {
            if (!block_header_valid(block))
            {
                ++g_corrupt_block_count;
                TINYOS_WARN_ON(true, "Kernel heap metadata corruption detected.");
                return nullptr;
            }

            if (payload_address(block) == address)
            {
                return block;
            }
        }

        return nullptr;
    }

    BlockHeader* append_region(size_t minimum_size)
    {
        size_t required_bytes = align_up(minimum_size);
        size_t region_bytes = required_bytes + header_bytes();
        size_t page_count = (region_bytes + tinyos::kernel::memory::frames::FrameSize - 1) / tinyos::kernel::memory::frames::FrameSize;
        if (page_count < DefaultGrowthPages)
        {
            page_count = DefaultGrowthPages;
        }

        const uintptr_t address = tinyos::kernel::memory::frames::allocate_pages(page_count);
        if (address == 0)
        {
            return nullptr;
        }

        auto* block = reinterpret_cast<BlockHeader*>(address);
    block->magic = BlockMagic;
    block->size = page_count * tinyos::kernel::memory::frames::FrameSize - header_bytes();
        block->is_free = true;
        block->next = nullptr;
        g_total_bytes += block->size;

        if (g_head == nullptr)
        {
            g_head = block;
            return block;
        }

        BlockHeader* tail = g_head;
        while (tail->next != nullptr)
        {
            if (!block_header_valid(tail))
            {
                ++g_corrupt_block_count;
                TINYOS_WARN_ON(true, "Kernel heap metadata corruption detected while growing heap.");
                return nullptr;
            }

            tail = tail->next;
        }

        tail->next = block;
        return block;
    }

    void split_block(BlockHeader* block, size_t requested_size)
    {
        const size_t aligned_size = align_up(requested_size);
        const size_t header_size = header_bytes();
        if (block->size <= aligned_size + header_size + MinimumSplitBytes)
        {
            return;
        }

        uintptr_t next_address = reinterpret_cast<uintptr_t>(block) + header_size + aligned_size;
        auto* next_block = reinterpret_cast<BlockHeader*>(next_address);
        next_block->magic = BlockMagic;
        next_block->size = block->size - aligned_size - header_size;
        next_block->is_free = true;
        next_block->next = block->next;

        block->size = aligned_size;
        block->next = next_block;
        g_total_bytes -= header_size;
    }

    void coalesce()
    {
        BlockHeader* block = g_head;
        while (block != nullptr && block->next != nullptr)
        {
            if (!block_header_valid(block) || !block_header_valid(block->next))
            {
                ++g_corrupt_block_count;
                TINYOS_WARN_ON(true, "Kernel heap metadata corruption detected during coalesce.");
                return;
            }

            uintptr_t block_end = reinterpret_cast<uintptr_t>(block) + header_bytes() + block->size;
            if (block->is_free && block->next->is_free && block_end == reinterpret_cast<uintptr_t>(block->next))
            {
                block->size += header_bytes() + block->next->size;
                block->next = block->next->next;
                g_total_bytes += header_bytes();
                continue;
            }

            block = block->next;
        }
    }
}

namespace tinyos::kernel::memory::heap
{
    void initialize()
    {
        if (g_initialized)
        {
            return;
        }

        if (append_region(tinyos::kernel::memory::frames::FrameSize) == nullptr)
        {
            kernel::panic("Failed to initialize kernel heap.");
        }

        g_initialized = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Kernel heap initialized.");
    }

    void* allocate(size_t size)
    {
        if (size == 0)
        {
            return nullptr;
        }

        if (!g_initialized)
        {
            initialize();
        }

        const size_t aligned_size = align_up(size);

        for (;;)
        {
            BlockHeader* block = g_head;
            while (block != nullptr)
            {
                if (!block_header_valid(block))
                {
                    ++g_corrupt_block_count;
                    TINYOS_WARN_ON(true, "Kernel heap metadata corruption detected during allocation.");
                    return nullptr;
                }

                if (block->is_free && block->size >= aligned_size)
                {
                    split_block(block, aligned_size);
                    block->is_free = false;
                    ++g_allocation_count;
                    return reinterpret_cast<void*>(payload_address(block));
                }

                block = block->next;
            }

            if (append_region(aligned_size) == nullptr)
            {
                return nullptr;
            }
        }
    }

    void free(void* pointer)
    {
        if (pointer == nullptr)
        {
            return;
        }

        if ((reinterpret_cast<uintptr_t>(pointer) & (Alignment - 1)) != 0)
        {
            ++g_invalid_free_count;
            TINYOS_WARN_ON(true, "Kernel heap rejected an unaligned free pointer.");
            return;
        }

        auto* block = find_block_for_payload(pointer);
        if (block == nullptr)
        {
            ++g_invalid_free_count;
            TINYOS_WARN_ON(true, "Kernel heap rejected an unknown free pointer.");
            return;
        }

        if (block->is_free)
        {
            ++g_double_free_count;
            TINYOS_WARN_ON(true, "Kernel heap rejected a double free.");
            return;
        }

        block->is_free = true;
        ++g_free_count;
        coalesce();
    }

    size_t total_bytes()
    {
        return g_total_bytes;
    }

    size_t free_bytes()
    {
        size_t total = 0;
        for (BlockHeader* block = g_head; block != nullptr; block = block->next)
        {
            if (block->is_free)
            {
                total += block->size;
            }
        }

        return total;
    }

    size_t used_bytes()
    {
        return total_bytes() - free_bytes();
    }

    size_t block_count()
    {
        size_t count = 0;
        for (BlockHeader* block = g_head; block != nullptr; block = block->next)
        {
            ++count;
        }

        return count;
    }

    size_t allocation_count()
    {
        return g_allocation_count;
    }

    size_t free_count()
    {
        return g_free_count;
    }

    size_t invalid_free_count()
    {
        return g_invalid_free_count;
    }

    size_t double_free_count()
    {
        return g_double_free_count;
    }

    size_t corrupt_block_count()
    {
        return g_corrupt_block_count;
    }

    bool state_valid()
    {
        size_t observed_total = 0;

        for (BlockHeader* block = g_head; block != nullptr; block = block->next)
        {
            if (!block_header_valid(block) || block->size == 0)
            {
                return false;
            }

            if ((payload_address(block) & (Alignment - 1)) != 0)
            {
                return false;
            }

            if (block->size > g_total_bytes || observed_total > g_total_bytes - block->size)
            {
                return false;
            }

            observed_total += block->size;
        }

        return observed_total == g_total_bytes;
    }
}
