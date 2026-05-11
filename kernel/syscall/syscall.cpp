#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/syscall/syscall.hpp>

namespace
{
    constexpr uintptr_t NullPageLimit = 0x1000;
    constexpr size_t MaxUserBufferBytes = 64 * 1024;
    constexpr size_t MaxArgumentCount = 4;

    const tinyos::kernel::syscall::BoundaryPolicy g_boundary_policy = {
        MaxArgumentCount,
        MaxUserBufferBytes,
        NullPageLimit,
        true,
        true
    };

    bool g_ready = false;
    size_t g_validation_failure_count = 0;
    size_t g_rejected_call_count = 0;

    tinyos::kernel::syscall::Result make_result(tinyos::kernel::syscall::Status status, uint32_t value = 0)
    {
        return { status, value };
    }

    bool range_overflows(uintptr_t address, size_t length)
    {
        return length > static_cast<size_t>(~static_cast<uintptr_t>(0) - address);
    }

    bool validate_buffer(uintptr_t address, size_t length, bool count_failure)
    {
        if (address < NullPageLimit)
        {
            if (count_failure)
            {
                ++g_validation_failure_count;
            }
            return false;
        }

        if (length == 0 || length > MaxUserBufferBytes)
        {
            if (count_failure)
            {
                ++g_validation_failure_count;
            }
            return false;
        }

        if (range_overflows(address, length))
        {
            if (count_failure)
            {
                ++g_validation_failure_count;
            }
            return false;
        }

        return true;
    }
}

namespace tinyos::kernel::syscall
{
    void initialize()
    {
        g_validation_failure_count = 0;
        g_rejected_call_count = 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Syscall ABI scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    uint32_t count()
    {
        return static_cast<uint32_t>(Number::Count);
    }

    size_t max_user_buffer_bytes()
    {
        return MaxUserBufferBytes;
    }

    const BoundaryPolicy& boundary_policy()
    {
        return g_boundary_policy;
    }

    bool is_known(Number number)
    {
        return static_cast<uint32_t>(number) < count();
    }

    bool is_known_number(uint32_t number)
    {
        return number < count();
    }

    bool validate_user_buffer(uintptr_t address, size_t length, BufferAccess access)
    {
        (void)access;
        return validate_buffer(address, length, true);
    }

    Result dispatch(const Request& request)
    {
        if (!is_known_number(request.number))
        {
            ++g_rejected_call_count;
            return make_result(Status::UnknownSyscall);
        }

        const auto number = static_cast<Number>(request.number);
        switch (number)
        {
        case Number::Write:
            if (!validate_user_buffer(request.arg0, request.arg1, BufferAccess::Read))
            {
                ++g_rejected_call_count;
                return request.arg1 == 0 || request.arg1 > MaxUserBufferBytes
                    ? make_result(Status::InvalidLength)
                    : make_result(Status::InvalidPointer);
            }
            return make_result(Status::Unsupported);
        case Number::Read:
            if (!validate_user_buffer(request.arg0, request.arg1, BufferAccess::Write))
            {
                ++g_rejected_call_count;
                return request.arg1 == 0 || request.arg1 > MaxUserBufferBytes
                    ? make_result(Status::InvalidLength)
                    : make_result(Status::InvalidPointer);
            }
            return make_result(Status::Unsupported);
        case Number::Open:
        case Number::Close:
        case Number::Spawn:
        case Number::Exit:
        case Number::Sleep:
            return make_result(Status::Unsupported);
        case Number::Count:
            break;
        }

        ++g_rejected_call_count;
        return make_result(Status::UnknownSyscall);
    }

    bool validation_self_test()
    {
        const size_t failures_before = g_validation_failure_count;
        const size_t rejected_before = g_rejected_call_count;
        const Request unknown_request = { count(), 0, 0, 0, 0 };
        const Request null_write_request = { static_cast<uint32_t>(Number::Write), 0, 8, 0, 0 };
        const Request oversized_write_request = { static_cast<uint32_t>(Number::Write), NullPageLimit, MaxUserBufferBytes + 1, 0, 0 };
        const Request accepted_shape_request = { static_cast<uint32_t>(Number::Write), NullPageLimit, 16, 0, 0 };

        const bool direct_valid = validate_buffer(NullPageLimit, 16, false);
        const bool direct_null_rejected = !validate_buffer(0, 16, false);
        const bool unknown_rejected = dispatch(unknown_request).status == Status::UnknownSyscall;
        const bool null_rejected = dispatch(null_write_request).status == Status::InvalidPointer;
        const bool oversized_rejected = dispatch(oversized_write_request).status == Status::InvalidLength;
        const bool accepted_validated = dispatch(accepted_shape_request).status == Status::Unsupported;

        g_validation_failure_count = failures_before;
        g_rejected_call_count = rejected_before;

        return direct_valid
            && direct_null_rejected
            && unknown_rejected
            && null_rejected
            && oversized_rejected
            && accepted_validated
            && boundary_policy_validation_self_test();
    }

    bool boundary_policy_validation_self_test()
    {
        return g_boundary_policy.max_argument_count == MaxArgumentCount &&
            g_boundary_policy.max_user_buffer_bytes == MaxUserBufferBytes &&
            g_boundary_policy.null_guard_bytes == NullPageLimit &&
            g_boundary_policy.reject_unknown_numbers &&
            g_boundary_policy.require_explicit_buffer_access;
    }

    size_t validation_failure_count()
    {
        return g_validation_failure_count;
    }

    size_t rejected_call_count()
    {
        return g_rejected_call_count;
    }

    const char* status_name(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "ok";
        case Status::UnknownSyscall:
            return "unknown-syscall";
        case Status::InvalidPointer:
            return "invalid-pointer";
        case Status::InvalidLength:
            return "invalid-length";
        case Status::Unsupported:
            return "unsupported";
        }

        return "unknown-status";
    }
}
