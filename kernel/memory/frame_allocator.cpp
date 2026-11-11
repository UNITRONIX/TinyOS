#include <tinyos/boot/multiboot.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>

namespace
{
    // Upper bound on managed physical memory for the 32-bit identity-mapped
    // kernel. The bitmap is allocated from the memory it describes rather than
    // reserved in .bss: a static 4 GiB bitmap costs 512 KiB of image space, which
    // was enough to make the heap initialisation fail on a 32 MiB machine.
    //
    // Declared as uint64_t: size_t is 32 bits wide on the i686 kernel, so a 4 GiB
    // ceiling expressed in size_t wraps to zero, which in turn makes MaxFrames
    // zero and every frame get clamped away. That exact bug produced a machine
    // with "frames=0 bitmap=0" and a heap that could not initialise.
    constexpr uint64_t MaxManagedBytes = 4ull * 1024ull * 1024ull * 1024ull;
    constexpr size_t MaxFrames = static_cast<size_t>(MaxManagedBytes / 4096ull);
    constexpr uint32_t UsableRegionType = 1;
    constexpr uintptr_t LowMemoryCutoff = 0x00100000;

    // Bitmap storage. `g_bitmap` points into the first usable region found by
    // the memory-map walk; that region is carved out before any frame is
    // released to the allocator.
    unsigned char* g_bitmap = nullptr;
    size_t g_bitmap_frames = 0;
    size_t g_bitmap_bytes = 0;
    size_t g_total_frames = 0;
    size_t g_free_frames = 0;
    size_t g_allocation_failure_count = 0;
    size_t g_invalid_free_count = 0;
    size_t g_double_free_count = 0;

    // Allocation hint. Rescanning from frame 0 makes every allocation O(total
    // RAM); starting where the last search succeeded keeps the common case
    // close to O(1) and only wraps once.
    size_t g_search_hint = 0;

    extern "C" char __kernel_start;
    extern "C" char __kernel_end;

    uint64_t align_up(uint64_t value, uint64_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint64_t align_down(uint64_t value, uint64_t alignment)
    {
        return value & ~(alignment - 1);
    }

    void set_used(size_t frame)
    {
        if (g_bitmap == nullptr)
        {
            return;
        }

        g_bitmap[frame / 8] &= static_cast<unsigned char>(~(1u << (frame % 8)));
    }

    void set_free(size_t frame)
    {
        if (g_bitmap == nullptr)
        {
            return;
        }

        g_bitmap[frame / 8] |= static_cast<unsigned char>(1u << (frame % 8));
    }

    bool is_used(size_t frame)
    {
        if (g_bitmap == nullptr)
        {
            // Treat everything as used before the map exists, so a premature
            // caller cannot hand out memory that has no bookkeeping yet.
            return true;
        }

        return (g_bitmap[frame / 8] & static_cast<unsigned char>(1u << (frame % 8))) == 0;
    }

    size_t string_length(const char* text)
    {
        size_t length = 0;
        while (text[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    void reserve_range(uint64_t begin, uint64_t end)
    {
        const uint64_t aligned_begin = align_down(begin, tinyos::kernel::memory::frames::FrameSize);
        const uint64_t aligned_end = align_up(end, tinyos::kernel::memory::frames::FrameSize);
        const uint64_t managed_limit = static_cast<uint64_t>(g_total_frames) * tinyos::kernel::memory::frames::FrameSize;

        uint64_t current = aligned_begin;
        if (current >= managed_limit)
        {
            return;
        }

        const uint64_t end_limit = aligned_end < managed_limit ? aligned_end : managed_limit;

        for (; current < end_limit; current += tinyos::kernel::memory::frames::FrameSize)
        {
            const size_t frame = static_cast<size_t>(current / tinyos::kernel::memory::frames::FrameSize);
            if (frame >= g_total_frames || is_used(frame))
            {
                continue;
            }

            set_used(frame);
            --g_free_frames;
        }
    }
}

namespace tinyos::kernel::memory::frames
{
    void initialize(uint32_t multiboot_info_addr)
    {
        g_total_frames = 0;
        g_free_frames = 0;
        g_allocation_failure_count = 0;
        g_invalid_free_count = 0;
        g_double_free_count = 0;
        g_bitmap = nullptr;
        g_bitmap_frames = 0;
        g_bitmap_bytes = 0;

        const uint64_t max_memory = map::total_bytes();
        g_total_frames = static_cast<size_t>(max_memory / FrameSize);
        if (g_total_frames > MaxFrames)
        {
            g_total_frames = MaxFrames;
        }

        // The identity map is sized from the detected memory, so every managed
        // frame is reachable. Previously the allocator was clamped to a 16 MiB
        // window, which silently discarded most of a machine's RAM.
        g_search_hint = 0;

        // Phase 1: find storage for the bitmap itself.
        //
        // The bitmap is sized by the machine's RAM, so it cannot live in .bss
        // without inflating the kernel image for everyone. It is carved out of
        // the *top* of the highest usable region instead of the bottom of the
        // first one: GRUB places the kernel at 1 MiB and the initrd modules
        // directly after the kernel image, so a bitmap starting at kernel end
        // silently overwrote the boot modules as soon as it grew large enough
        // (observable at 3 GiB, where the bitmap is a few hundred KiB).
        g_bitmap_bytes = (g_total_frames + 7) / 8;
        g_bitmap_bytes = static_cast<size_t>(align_up(g_bitmap_bytes, FrameSize));
        g_bitmap_frames = g_bitmap_bytes / FrameSize;

        for (size_t region_index = 0; region_index < map::region_count(); ++region_index)
        {
            const auto& region = map::region(region_index);
            if (region.type != UsableRegionType)
            {
                continue;
            }

            uint64_t begin = region.base;
            uint64_t end = region.base + region.length;
            if (end <= LowMemoryCutoff)
            {
                continue;
            }

            if (begin < LowMemoryCutoff)
            {
                begin = LowMemoryCutoff;
            }

            begin = align_up(begin, FrameSize);
            end = align_down(end, FrameSize);

            if (end <= begin || (end - begin) < g_bitmap_bytes)
            {
                continue;
            }

            const uint64_t candidate = end - g_bitmap_bytes;
            if (candidate <= begin)
            {
                continue;
            }

            // Prefer the highest candidate so the reserved range sits above the
            // kernel and its boot modules.
            if (g_bitmap == nullptr || candidate > reinterpret_cast<uintptr_t>(g_bitmap))
            {
                g_bitmap = reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(candidate));
            }
        }

        if (g_bitmap == nullptr)
        {
            kernel::klog::write_line(kernel::klog::Level::Error, "Frame allocator has no space for its bitmap.");
            return;
        }

        // Phase 1b: everything starts reserved. Only usable regions are freed
        // below, so nothing is handed out before the map describes reality.
        for (size_t index = 0; index < g_bitmap_bytes; ++index)
        {
            g_bitmap[index] = 0;
        }

        for (size_t region_index = 0; region_index < map::region_count(); ++region_index)
        {
            const auto& region = map::region(region_index);
            if (region.type != UsableRegionType)
            {
                continue;
            }

            uint64_t begin = region.base;
            uint64_t end = region.base + region.length;
            const uint64_t managed_limit = static_cast<uint64_t>(g_total_frames) * FrameSize;

            if (end <= LowMemoryCutoff)
            {
                continue;
            }

            if (begin < LowMemoryCutoff)
            {
                begin = LowMemoryCutoff;
            }

            begin = align_up(begin, FrameSize);
            end = align_down(end, FrameSize);

            if (begin >= managed_limit)
            {
                continue;
            }

            if (end > managed_limit)
            {
                end = managed_limit;
            }

            for (uint64_t address = begin; address < end; address += FrameSize)
            {
                const size_t frame = static_cast<size_t>(address / FrameSize);
                if (frame >= g_total_frames || !is_used(frame))
                {
                    continue;
                }

                set_free(frame);
                ++g_free_frames;
            }
        }

        reserve_range(0, LowMemoryCutoff);
        reserve_range(reinterpret_cast<uintptr_t>(&__kernel_start), reinterpret_cast<uintptr_t>(&__kernel_end));

        // Reserve the bitmap storage itself, which was carved out of the first
        // usable region above.
        const uintptr_t bitmap_address = reinterpret_cast<uintptr_t>(g_bitmap);
        reserve_range(bitmap_address, bitmap_address + g_bitmap_bytes);

        const auto* info = reinterpret_cast<const boot::multiboot::Info*>(multiboot_info_addr);
        reserve_range(multiboot_info_addr, multiboot_info_addr + sizeof(boot::multiboot::Info));
        if ((info->flags & boot::multiboot::FlagModules) != 0)
        {
            reserve_range(info->mods_addr, info->mods_addr + info->mods_count * sizeof(boot::multiboot::ModuleEntry));

            const auto* modules = reinterpret_cast<const boot::multiboot::ModuleEntry*>(info->mods_addr);
            for (uint32_t index = 0; index < info->mods_count; ++index)
            {
                reserve_range(modules[index].mod_start, modules[index].mod_end);

                if (modules[index].string != 0)
                {
                    const char* name = reinterpret_cast<const char*>(modules[index].string);
                    reserve_range(modules[index].string, modules[index].string + string_length(name) + 1);
                }
            }
        }

        if ((info->flags & boot::multiboot::FlagMemoryMap) != 0)
        {
            reserve_range(info->mmap_addr, info->mmap_addr + info->mmap_length);
        }

        tinyos::kernel::klog::write_line(kernel::klog::Level::Info, "Physical frame allocator initialized.");

        // Report the managed pool on the serial log. The VGA boot summary is not
        // readable from a headless test, and the size of the managed pool is the
        // whole point of removing the old fixed memory ceiling.
        {
            char summary[64];
            size_t length = 0;
            auto append = [&summary, &length](const char* text) {
                while (*text != '\0' && length + 1 < sizeof(summary))
                {
                    summary[length++] = *text++;
                }
            };
            auto append_dec = [&summary, &length](size_t value) {
                char digits[21];
                int index = 20;
                do
                {
                    digits[index--] = static_cast<char>('0' + (value % 10));
                    value /= 10;
                } while (value != 0);
                while (index + 1 < 21 && length + 1 < sizeof(summary))
                {
                    summary[length++] = digits[++index];
                }
            };

            append("Frame pool: ");
            append_dec((g_free_frames * FrameSize) / (1024 * 1024));
            append(" MiB free of ");
            append_dec((g_total_frames * FrameSize) / (1024 * 1024));
            append(" MiB managed.");
            summary[length] = '\0';
            tinyos::kernel::klog::write_line(kernel::klog::Level::Info, summary);
        }

        if (g_free_frames == 0)
        {
            kernel::klog::write_line(kernel::klog::Level::Error, "Frame allocator released no frames; memory map or bitmap placement is wrong.");
        }
    }

    uintptr_t allocate()
    {
        return allocate_pages(1);
    }

    uintptr_t allocate_pages(size_t count)
    {
        if (count == 0 || count > g_free_frames)
        {
            if (count > g_free_frames)
            {
                ++g_allocation_failure_count;
            }

            return 0;
        }

        // Two passes: from the hint to the end, then wrapping to the start.
        // Without the hint every allocation rescans from frame 0 and the cost
        // grows with installed RAM.
        for (int pass = 0; pass < 2; ++pass)
        {
            const size_t begin = (pass == 0) ? g_search_hint : 0;
            const size_t end = (pass == 0) ? g_total_frames : g_search_hint;

            size_t run_start = 0;
            size_t run_length = 0;

            for (size_t frame = begin; frame < end; ++frame)
            {
                if (is_used(frame))
                {
                    run_length = 0;
                    continue;
                }

                if (run_length == 0)
                {
                    run_start = frame;
                }

                ++run_length;
                if (run_length != count)
                {
                    continue;
                }

                for (size_t current = run_start; current < run_start + count; ++current)
                {
                    set_used(current);
                }

                g_free_frames -= count;
                g_search_hint = run_start + count;

                return static_cast<uintptr_t>(run_start * FrameSize);
            }
        }

        ++g_allocation_failure_count;
        return 0;
    }

    void free(uintptr_t address)
    {
        free_pages(address, 1);
    }

    void free_pages(uintptr_t address, size_t count)
    {
        if (address == 0 || count == 0)
        {
            return;
        }

        if ((address % FrameSize) != 0)
        {
            ++g_invalid_free_count;
            TINYOS_WARN_ON_CATEGORY(true, tinyos::kernel::klog::WarningCategory::Memory, "Frame allocator rejected an unaligned free address.");
            return;
        }

        const size_t first_frame = static_cast<size_t>(address / FrameSize);
        for (size_t index = 0; index < count; ++index)
        {
            const size_t frame = first_frame + index;
            if (frame >= g_total_frames)
            {
                ++g_invalid_free_count;
                TINYOS_WARN_ON_CATEGORY(true, tinyos::kernel::klog::WarningCategory::Memory, "Frame allocator rejected an out-of-range free address.");
                continue;
            }

            if (!is_used(frame))
            {
                ++g_double_free_count;
                TINYOS_WARN_ON_CATEGORY(true, tinyos::kernel::klog::WarningCategory::Memory, "Frame allocator rejected a double free.");
                continue;
            }

            set_free(frame);
            ++g_free_frames;
        }
    }

    size_t total_frames()
    {
        return g_total_frames;
    }

    size_t free_frames()
    {
        return g_free_frames;
    }

    size_t reserved_frames()
    {
        return g_total_frames - g_free_frames;
    }

    size_t allocation_failure_count()
    {
        return g_allocation_failure_count;
    }

    size_t invalid_free_count()
    {
        return g_invalid_free_count;
    }

    size_t double_free_count()
    {
        return g_double_free_count;
    }

    bool accounting_valid()
    {
        size_t observed_free = 0;
        for (size_t frame = 0; frame < g_total_frames; ++frame)
        {
            if (!is_used(frame))
            {
                ++observed_free;
            }
        }

        return g_free_frames <= g_total_frames && observed_free == g_free_frames;
    }
}
