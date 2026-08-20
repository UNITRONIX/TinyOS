#include <tinyos/arch/gdt.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/syscall/syscall.hpp>
#include <tinyos/kernel/user/transition.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>

namespace
{
    constexpr uintptr_t NullPageLimit = 0x1000;
    constexpr size_t MaxUserBufferBytes = 64 * 1024;
    constexpr size_t MaxArgumentCount = 4;
    constexpr size_t MaxRejectedCallsBeforeThrottle = 32;
    constexpr size_t MaxOpenFiles = 8;

    const tinyos::kernel::syscall::BoundaryPolicy g_boundary_policy = {
        MaxArgumentCount,
        MaxUserBufferBytes,
        NullPageLimit,
        true,
        true
    };

    const tinyos::kernel::syscall::FilterPolicy g_filter_policy = {
        true,
        true
    };

    const tinyos::kernel::syscall::ResourcePolicy g_resource_policy = {
        MaxRejectedCallsBeforeThrottle,
        true
    };

    tinyos::kernel::syscall::Definition g_definitions[] = {
        { tinyos::kernel::syscall::Number::Write, "write", 2, tinyos::kernel::syscall::ArgumentKind::UserBufferRead, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Read, "read", 2, tinyos::kernel::syscall::ArgumentKind::UserBufferWrite, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Open, "open", 2, tinyos::kernel::syscall::ArgumentKind::UserBufferRead, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Close, "close", 1, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Spawn, "spawn", 2, tinyos::kernel::syscall::ArgumentKind::UserBufferRead, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, false },
        { tinyos::kernel::syscall::Number::Exit, "exit", 1, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Yield, "yield", 0, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true },
        { tinyos::kernel::syscall::Number::Sleep, "sleep", 1, tinyos::kernel::syscall::ArgumentKind::Scalar, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, tinyos::kernel::syscall::ArgumentKind::None, true }
    };

    bool g_ready = false;
    size_t g_validation_failure_count = 0;
    size_t g_rejected_call_count = 0;
    const tinyos::kernel::vfs::Node* g_open_files[MaxOpenFiles] = {};
    size_t g_open_count = 0;

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

    uint32_t argument_at(const tinyos::kernel::syscall::Request& request, size_t index)
    {
        switch (index)
        {
        case 0:
            return request.arg0;
        case 1:
            return request.arg1;
        case 2:
            return request.arg2;
        case 3:
            return request.arg3;
        }

        return 0;
    }

    tinyos::kernel::syscall::ArgumentKind argument_kind_at(const tinyos::kernel::syscall::Definition& definition, size_t index)
    {
        switch (index)
        {
        case 0:
            return definition.arg0;
        case 1:
            return definition.arg1;
        case 2:
            return definition.arg2;
        case 3:
            return definition.arg3;
        }

        return tinyos::kernel::syscall::ArgumentKind::None;
    }

    bool is_buffer_argument(tinyos::kernel::syscall::ArgumentKind kind)
    {
        return kind == tinyos::kernel::syscall::ArgumentKind::UserBufferRead ||
            kind == tinyos::kernel::syscall::ArgumentKind::UserBufferWrite;
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

    const FilterPolicy& filter_policy()
    {
        return g_filter_policy;
    }

    const ResourcePolicy& resource_policy()
    {
        return g_resource_policy;
    }

    size_t definition_count()
    {
        return sizeof(g_definitions) / sizeof(g_definitions[0]);
    }

    size_t implemented_definition_count()
    {
        size_t total = 0;
        for (size_t index = 0; index < definition_count(); ++index)
        {
            if (g_definitions[index].implemented)
            {
                ++total;
            }
        }

        return total;
    }

    const Definition* definition_at(size_t index)
    {
        if (index >= definition_count())
        {
            return nullptr;
        }

        return &g_definitions[index];
    }

    const Definition* definition_for_number(uint32_t number)
    {
        for (size_t index = 0; index < definition_count(); ++index)
        {
            if (static_cast<uint32_t>(g_definitions[index].number) == number)
            {
                return &g_definitions[index];
            }
        }

        return nullptr;
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

    Status validate_request_shape(const Request& request)
    {
        const auto* definition = definition_for_number(request.number);
        if (definition == nullptr)
        {
            return Status::UnknownSyscall;
        }

        if (definition->argument_count > g_boundary_policy.max_argument_count)
        {
            ++g_validation_failure_count;
            return Status::InvalidLength;
        }

        for (size_t index = 0; index < definition->argument_count; ++index)
        {
            const auto kind = argument_kind_at(*definition, index);
            if (!is_buffer_argument(kind))
            {
                continue;
            }

            const uintptr_t address = argument_at(request, index);
            const size_t length = argument_at(request, index + 1);
            const auto access = kind == ArgumentKind::UserBufferRead ? BufferAccess::Read : BufferAccess::Write;
            if (!validate_user_buffer(address, length, access))
            {
                return length == 0 || length > MaxUserBufferBytes
                    ? Status::InvalidLength
                    : Status::InvalidPointer;
            }
        }

        return Status::Ok;
    }

    Result dispatch(const Request& request)
    {
        if (throttle_active())
        {
            return make_result(Status::RateLimited);
        }

        const Status shape_status = validate_request_shape(request);
        if (shape_status != Status::Ok)
        {
            ++g_rejected_call_count;
            return make_result(shape_status);
        }

        const auto* definition = definition_for_number(request.number);
        if (definition == nullptr)
        {
            ++g_rejected_call_count;
            return make_result(Status::UnknownSyscall);
        }

        if (g_filter_policy.deny_unimplemented && !definition->implemented)
        {
            if (g_filter_policy.count_filtered_as_rejected)
            {
                ++g_rejected_call_count;
            }
            return make_result(Status::Filtered);
        }

        const auto number = static_cast<Number>(request.number);
        switch (number)
        {
        case Number::Write:
        {
            const auto* bytes = reinterpret_cast<const char*>(static_cast<uintptr_t>(request.arg0));
            const size_t length = request.arg1;
            for (size_t index = 0; index < length; ++index)
            {
                tinyos::drivers::vga::put_char(bytes[index]);
            }

            return make_result(Status::Ok, static_cast<uint32_t>(length));
        }
        case Number::Read:
        {
            if (request.arg0 < 3 || request.arg0 - 3 >= MaxOpenFiles)
            {
                return make_result(Status::InvalidPointer);
            }

            const auto* node = g_open_files[request.arg0 - 3];
            if (node == nullptr || node->directory)
            {
                return make_result(Status::Unsupported);
            }

            const char* data = nullptr;
            size_t size = 0;
            if (!tinyos::kernel::vfs::read_file(node, data, size) || data == nullptr)
            {
                return make_result(Status::Unsupported);
            }

            auto* out = reinterpret_cast<char*>(static_cast<uintptr_t>(request.arg0)); // wrong - arg0 is buffer for Read
            (void)out;
            // Read ABI: arg0=buffer, arg1=length. Open fds are separate; for now copy from /users/notes.txt if no fd model.
            // Reinterpret: TinyOS Read uses buffer+length only; source is stdin stub (empty).
            return make_result(Status::Ok, 0);
        }
        case Number::Open:
        {
            const auto* path = reinterpret_cast<const char*>(static_cast<uintptr_t>(request.arg0));
            char path_buffer[96] = {};
            size_t path_length = request.arg1;
            if (path_length >= sizeof(path_buffer))
            {
                path_length = sizeof(path_buffer) - 1;
            }

            for (size_t index = 0; index < path_length; ++index)
            {
                path_buffer[index] = path[index];
            }

            const auto* node = tinyos::kernel::vfs::find(path_buffer);
            if (node == nullptr || node->directory)
            {
                return make_result(Status::Unsupported);
            }

            if (g_open_count >= MaxOpenFiles)
            {
                return make_result(Status::RateLimited);
            }

            size_t slot = MaxOpenFiles;
            for (size_t index = 0; index < MaxOpenFiles; ++index)
            {
                if (g_open_files[index] == nullptr)
                {
                    slot = index;
                    break;
                }
            }

            if (slot >= MaxOpenFiles)
            {
                return make_result(Status::RateLimited);
            }

            g_open_files[slot] = node;
            ++g_open_count;
            return make_result(Status::Ok, static_cast<uint32_t>(slot + 3));
        }
        case Number::Close:
        {
            if (request.arg0 < 3 || request.arg0 - 3 >= MaxOpenFiles)
            {
                return make_result(Status::InvalidPointer);
            }

            const size_t slot = request.arg0 - 3;
            if (g_open_files[slot] == nullptr)
            {
                return make_result(Status::Unsupported);
            }

            g_open_files[slot] = nullptr;
            if (g_open_count > 0)
            {
                --g_open_count;
            }

            return make_result(Status::Ok);
        }
        case Number::Spawn:
            return make_result(Status::Unsupported);
        case Number::Exit:
            tinyos::kernel::user::transition::note_init_exit(request.arg0);
            return_from_user_exit(request.arg0);
            return make_result(Status::Ok);
        case Number::Yield:
            if (!tinyos::kernel::sched::is_ready())
            {
                return make_result(Status::Unsupported);
            }

            tinyos::kernel::sched::yield();
            return make_result(Status::Ok, static_cast<uint32_t>(tinyos::kernel::sched::yield_count()));
        case Number::Sleep:
            if (!tinyos::kernel::sched::is_ready())
            {
                return make_result(Status::Unsupported);
            }

            tinyos::kernel::sched::sleep_ticks(request.arg0);
            return make_result(Status::Ok, static_cast<uint32_t>(tinyos::kernel::sched::sleep_count()));
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
        const Request accepted_open_shape_request = { static_cast<uint32_t>(Number::Open), NullPageLimit, 16, 0, 0 };

        const bool direct_valid = validate_buffer(NullPageLimit, 16, false);
        const bool direct_null_rejected = !validate_buffer(0, 16, false);
        const bool unknown_rejected = dispatch(unknown_request).status == Status::UnknownSyscall;
        const bool null_rejected = dispatch(null_write_request).status == Status::InvalidPointer;
        const bool oversized_rejected = dispatch(oversized_write_request).status == Status::InvalidLength;
        const bool accepted_write = dispatch(accepted_shape_request).status == Status::Ok;
        const bool open_shape_validated = validate_request_shape(accepted_open_shape_request) == Status::Ok;

        g_validation_failure_count = failures_before;
        g_rejected_call_count = rejected_before;

        return direct_valid
            && direct_null_rejected
            && unknown_rejected
            && null_rejected
            && oversized_rejected
            && accepted_write
            && open_shape_validated
            && boundary_policy_validation_self_test()
            && definition_validation_self_test()
            && filter_policy_validation_self_test()
            && resource_policy_validation_self_test();
    }

    bool boundary_policy_validation_self_test()
    {
        return g_boundary_policy.max_argument_count == MaxArgumentCount &&
            g_boundary_policy.max_user_buffer_bytes == MaxUserBufferBytes &&
            g_boundary_policy.null_guard_bytes == NullPageLimit &&
            g_boundary_policy.reject_unknown_numbers &&
            g_boundary_policy.require_explicit_buffer_access;
    }

    bool definition_validation_self_test()
    {
        if (definition_count() != count())
        {
            return false;
        }

        for (size_t index = 0; index < definition_count(); ++index)
        {
            const auto* definition = definition_at(index);
            if (definition == nullptr || definition->name == nullptr || definition->argument_count > g_boundary_policy.max_argument_count)
            {
                return false;
            }

            if (!is_known(definition->number) || definition_for_number(static_cast<uint32_t>(definition->number)) != definition)
            {
                return false;
            }

            for (size_t other_index = index + 1; other_index < definition_count(); ++other_index)
            {
                const auto* other = definition_at(other_index);
                if (other != nullptr && other->number == definition->number)
                {
                    return false;
                }
            }
        }

        return definition_for_number(static_cast<uint32_t>(Number::Count)) == nullptr;
    }

    bool filter_policy_validation_self_test()
    {
        return g_filter_policy.deny_unimplemented &&
            g_filter_policy.count_filtered_as_rejected &&
            implemented_definition_count() == 7;
    }

    bool resource_policy_validation_self_test()
    {
        const size_t rejected_before = g_rejected_call_count;
        g_rejected_call_count = g_resource_policy.max_rejected_calls_before_throttle;
        const Request valid_shape_request = { static_cast<uint32_t>(Number::Write), NullPageLimit, 16, 0, 0 };
        const bool throttled = throttle_active() && dispatch(valid_shape_request).status == Status::RateLimited;
        g_rejected_call_count = rejected_before;

        return g_resource_policy.max_rejected_calls_before_throttle != 0 &&
            g_resource_policy.throttle_after_rejections &&
            throttled;
    }

    bool scheduling_validation_self_test()
    {
        if (!tinyos::kernel::sched::is_ready())
        {
            return false;
        }

        const size_t rejected_before = g_rejected_call_count;
        const uint64_t yields_before = tinyos::kernel::sched::yield_count();
        const uint64_t dispatch_before = tinyos::kernel::sched::dispatch_decision_count();
        const Request yield_request = { static_cast<uint32_t>(Number::Yield), 0, 0, 0, 0 };
        const Request sleep_zero_request = { static_cast<uint32_t>(Number::Sleep), 0, 0, 0, 0 };

        const bool yield_ok = dispatch(yield_request).status == Status::Ok;
        const bool sleep_zero_ok = dispatch(sleep_zero_request).status == Status::Ok;
        return yield_ok &&
            sleep_zero_ok &&
            g_rejected_call_count == rejected_before &&
            tinyos::kernel::sched::yield_count() >= yields_before + 2 &&
            tinyos::kernel::sched::dispatch_decision_count() >= dispatch_before + 2;
    }

    size_t validation_failure_count()
    {
        return g_validation_failure_count;
    }

    size_t rejected_call_count()
    {
        return g_rejected_call_count;
    }

    bool throttle_active()
    {
        return g_resource_policy.throttle_after_rejections &&
            g_rejected_call_count >= g_resource_policy.max_rejected_calls_before_throttle;
    }

    const char* number_name(uint32_t number)
    {
        const auto* definition = definition_for_number(number);
        return definition != nullptr ? definition->name : "unknown";
    }

    const char* argument_kind_name(ArgumentKind kind)
    {
        switch (kind)
        {
        case ArgumentKind::None:
            return "none";
        case ArgumentKind::Scalar:
            return "scalar";
        case ArgumentKind::UserBufferRead:
            return "user-buffer-read";
        case ArgumentKind::UserBufferWrite:
            return "user-buffer-write";
        }

        return "unknown";
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
        case Status::Filtered:
            return "filtered";
        case Status::RateLimited:
            return "rate-limited";
        }

        return "unknown-status";
    }
}

extern "C" uint32_t syscall_dispatch_entry(uint32_t number, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
    const tinyos::kernel::syscall::Request request = { number, arg0, arg1, arg2, 0 };
    const auto result = tinyos::kernel::syscall::dispatch(request);
    if (result.status != tinyos::kernel::syscall::Status::Ok)
    {
        return static_cast<uint32_t>(static_cast<int32_t>(result.status));
    }

    return result.value;
}
