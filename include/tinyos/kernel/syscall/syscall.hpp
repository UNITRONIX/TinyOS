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
        Sleep = 6,
        Count = 7
    };

    enum class Status : int32_t
    {
        Ok = 0,
        UnknownSyscall = -1,
        InvalidPointer = -2,
        InvalidLength = -3,
        Unsupported = -4
    };

    enum class BufferAccess : uint32_t
    {
        Read,
        Write
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

    void initialize();
    bool is_ready();
    uint32_t count();
    size_t max_user_buffer_bytes();
    const BoundaryPolicy& boundary_policy();
    bool is_known(Number number);
    bool is_known_number(uint32_t number);
    bool validate_user_buffer(uintptr_t address, size_t length, BufferAccess access);
    Result dispatch(const Request& request);
    bool validation_self_test();
    bool boundary_policy_validation_self_test();
    size_t validation_failure_count();
    size_t rejected_call_count();
    const char* status_name(Status status);
}
