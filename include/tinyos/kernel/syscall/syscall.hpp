#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::syscall
{
    enum class Number : uint32_t
    {
        Write = 0,
        Read = 1,
        Open = 2,
        Close = 3,
        Spawn = 4,
        Exit = 5,
        Yield = 6,
        Sleep = 7,
        Count = 8
    };

    enum class Status : int32_t
    {
        Ok = 0,
        UnknownSyscall = -1,
        InvalidPointer = -2,
        InvalidLength = -3,
        Unsupported = -4,
        Filtered = -5,
        RateLimited = -6
    };

    enum class BufferAccess : uint32_t
    {
        Read,
        Write
    };

    enum class ArgumentKind : uint32_t
    {
        None,
        Scalar,
        UserBufferRead,
        UserBufferWrite
    };

    struct Request
    {
        uint32_t number;
        uint32_t arg0;
        uint32_t arg1;
        uint32_t arg2;
        uint32_t arg3;
    };

    struct Result
    {
        Status status;
        uint32_t value;
    };

    struct BoundaryPolicy
    {
        size_t max_argument_count;
        size_t max_user_buffer_bytes;
        uintptr_t null_guard_bytes;
        bool reject_unknown_numbers;
        bool require_explicit_buffer_access;
    };

    struct Definition
    {
        Number number;
        const char* name;
        size_t argument_count;
        ArgumentKind arg0;
        ArgumentKind arg1;
        ArgumentKind arg2;
        ArgumentKind arg3;
        bool implemented;
    };

    struct FilterPolicy
    {
        bool deny_unimplemented;
        bool count_filtered_as_rejected;
    };

    struct ResourcePolicy
    {
        size_t max_rejected_calls_before_throttle;
        bool throttle_after_rejections;
    };

    void initialize();
    bool is_ready();
    uint32_t count();
    size_t max_user_buffer_bytes();
    const BoundaryPolicy& boundary_policy();
    const FilterPolicy& filter_policy();
    const ResourcePolicy& resource_policy();
    size_t definition_count();
    size_t implemented_definition_count();
    const Definition* definition_at(size_t index);
    const Definition* definition_for_number(uint32_t number);
    bool is_known(Number number);
    bool is_known_number(uint32_t number);
    bool validate_user_buffer(uintptr_t address, size_t length, BufferAccess access);
    Status validate_request_shape(const Request& request);
    Result dispatch(const Request& request);
    bool validation_self_test();
    bool boundary_policy_validation_self_test();
    bool definition_validation_self_test();
    bool filter_policy_validation_self_test();
    bool resource_policy_validation_self_test();
    bool scheduling_validation_self_test();
    size_t validation_failure_count();
    size_t rejected_call_count();
    bool throttle_active();
    const char* number_name(uint32_t number);
    const char* argument_kind_name(ArgumentKind kind);
    const char* status_name(Status status);
}
