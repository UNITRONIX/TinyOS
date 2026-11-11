#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/memory/user_access.hpp>

namespace
{
    constexpr uintptr_t NullGuardBytes = 4096;
    constexpr size_t MaxTransferBytes = 64 * 1024;

    tinyos::kernel::memory::user_access::Policy g_policy = {
        NullGuardBytes,
        MaxTransferBytes
    };

    bool g_ready = false;
    uint64_t g_rejected_count = 0;
    uint64_t g_copied_bytes = 0;

    bool range_overflows(uintptr_t address, size_t length)
    {
        // Reject any range whose end cannot be represented. Without this the
        // page-walk below would loop for an unbounded number of pages.
        return length > static_cast<size_t>(~static_cast<uintptr_t>(0) - address);
    }

    tinyos::kernel::memory::user_access::Status probe_pages(uintptr_t address, size_t length, bool for_write)
    {
        using Status = tinyos::kernel::memory::user_access::Status;

        if (address == 0)
        {
            return Status::NullPointer;
        }

        if (length == 0)
        {
            return Status::Ok;
        }

        if (address < g_policy.null_guard_bytes)
        {
            return Status::NullPointer;
        }

        if (length > g_policy.max_bytes)
        {
            return Status::LengthTooLarge;
        }

        if (range_overflows(address, length))
        {
            return Status::Overflow;
        }

        const uintptr_t page_size = tinyos::kernel::memory::frames::FrameSize;
        const uintptr_t last_address = address + length - 1;

        for (uintptr_t page = address & ~(page_size - 1); page <= last_address; page += page_size)
        {
            tinyos::kernel::memory::paging::PageMapping mapping;
            if (!tinyos::kernel::memory::paging::mapping_for(page, mapping))
            {
                return Status::NotMapped;
            }

            if (!mapping.present)
            {
                return Status::NotMapped;
            }

            // A user buffer must be reachable from ring 3. The bootstrap map
            // marks kernel pages as supervisor-only, so this rejects an attempt
            // to pass a kernel pointer into a syscall.
            if ((mapping.flags & tinyos::kernel::memory::paging::PageFlagUser) == 0)
            {
                return Status::NotUserAccessible;
            }

            if (for_write && (mapping.flags & tinyos::kernel::memory::paging::PageFlagWrite) == 0)
            {
                return Status::NotWritable;
            }
        }

        return Status::Ok;
    }
}

namespace tinyos::kernel::memory::user_access
{
    void initialize()
    {
        g_rejected_count = 0;
        g_copied_bytes = 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "User memory access checks ready.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    const Policy& policy()
    {
        return g_policy;
    }

    Status probe(uintptr_t address, size_t length, bool for_write)
    {
        const Status status = probe_pages(address, length, for_write);
        if (status != Status::Ok)
        {
            ++g_rejected_count;
        }

        return status;
    }

    Status copy_from_user(void* kernel_destination, uintptr_t user_source, size_t length, size_t& copied)
    {
        copied = 0;

        if (kernel_destination == nullptr)
        {
            ++g_rejected_count;
            return Status::NullPointer;
        }

        const Status status = probe(user_source, length, false);
        if (status != Status::Ok)
        {
            return status;
        }

        const auto* source = reinterpret_cast<const unsigned char*>(user_source);
        auto* destination = static_cast<unsigned char*>(kernel_destination);
        for (size_t index = 0; index < length; ++index)
        {
            destination[index] = source[index];
        }

        copied = length;
        g_copied_bytes += length;
        return Status::Ok;
    }

    Status copy_to_user(uintptr_t user_destination, const void* kernel_source, size_t length, size_t& copied)
    {
        copied = 0;

        if (kernel_source == nullptr)
        {
            ++g_rejected_count;
            return Status::NullPointer;
        }

        const Status status = probe(user_destination, length, true);
        if (status != Status::Ok)
        {
            return status;
        }

        auto* destination = reinterpret_cast<unsigned char*>(user_destination);
        const auto* source = static_cast<const unsigned char*>(kernel_source);
        for (size_t index = 0; index < length; ++index)
        {
            destination[index] = source[index];
        }

        copied = length;
        g_copied_bytes += length;
        return Status::Ok;
    }

    Status copy_string_from_user(char* kernel_destination, size_t capacity, uintptr_t user_source, size_t& copied)
    {
        copied = 0;

        if (kernel_destination == nullptr || capacity == 0)
        {
            ++g_rejected_count;
            return Status::NullPointer;
        }

        if (user_source < g_policy.null_guard_bytes)
        {
            ++g_rejected_count;
            return Status::NullPointer;
        }

        const auto* source = reinterpret_cast<const char*>(user_source);
        for (size_t index = 0; index + 1 < capacity; ++index)
        {
            // Validate one byte at a time: a string can end anywhere, so probing
            // the full capacity up front would reject a valid short string that
            // ends before an unmapped page.
            const Status status = probe(user_source + index, 1, false);
            if (status != Status::Ok)
            {
                kernel_destination[index] = '\0';
                copied = index;
                return status;
            }

            const char character = source[index];
            kernel_destination[index] = character;
            if (character == '\0')
            {
                copied = index;
                return Status::Ok;
            }
        }

        // Ran out of room without seeing a terminator.
        kernel_destination[capacity - 1] = '\0';
        copied = capacity - 1;
        ++g_rejected_count;
        return Status::LengthTooLarge;
    }

    uint64_t rejected_count()
    {
        return g_rejected_count;
    }

    uint64_t copied_bytes()
    {
        return g_copied_bytes;
    }

    const char* status_name(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "ok";
        case Status::NullPointer:
            return "null-pointer";
        case Status::NotMapped:
            return "not-mapped";
        case Status::NotUserAccessible:
            return "not-user-accessible";
        case Status::NotWritable:
            return "not-writable";
        case Status::LengthTooLarge:
            return "length-too-large";
        case Status::Overflow:
            return "overflow";
        }

        return "unknown";
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        // Behavioural checks that must hold on any boot. They use addresses the
        // bootstrap map definitely does and does not cover, so the test runs
        // without needing a user process.
        size_t copied = 0;

        const bool null_rejected = probe(0, 16, false) == Status::NullPointer;
        const bool low_address_rejected = probe(1, 16, false) == Status::NullPointer;
        const bool oversize_rejected = probe(0x40000000u, g_policy.max_bytes + 1, false) == Status::LengthTooLarge;
        const bool overflow_rejected = probe(0xFFFFFFF0u, 0x40, false) == Status::Overflow;

        // A kernel-only page (the kernel image) must not be accepted as a user
        // buffer even though it is mapped and present.
        const uintptr_t kernel_address = reinterpret_cast<uintptr_t>(&g_ready);
        const bool kernel_page_rejected = probe(kernel_address, 4, false) == Status::NotUserAccessible;

        char destination[8] = {};
        const bool copy_from_kernel_rejected = copy_from_user(destination, kernel_address, 4, copied) == Status::NotUserAccessible &&
            copied == 0;

        return null_rejected &&
            low_address_rejected &&
            oversize_rejected &&
            overflow_rejected &&
            kernel_page_rejected &&
            copy_from_kernel_rejected;
    }
}
