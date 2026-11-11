#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::memory::user_access
{
    // Result of a user-memory transfer. The kernel never faults on a bad user
    // pointer; it reports a status and leaves the caller to fail the syscall.
    enum class Status : uint32_t
    {
        Ok,
        NullPointer,
        NotMapped,
        NotUserAccessible,
        NotWritable,
        LengthTooLarge,
        Overflow
    };

    struct Policy
    {
        // Addresses below this are treated as null-pointer dereferences. Keeping
        // the first page unmapped turns the most common C bug into a clean
        // failure instead of a kernel read of whatever lives at address 0.
        uintptr_t null_guard_bytes;

        // Largest single transfer the kernel accepts from a syscall.
        size_t max_bytes;
    };

    void initialize();
    bool is_ready();
    const Policy& policy();

    // Verifies that [address, address+length) lies entirely in user-accessible
    // pages with the required rights, by walking the page tables. A range check
    // alone is not enough: the address may be unmapped, may belong to the kernel,
    // or may be a guard page.
    Status probe(uintptr_t address, size_t length, bool for_write);

    // Copies between user and kernel memory. On any invalid page the transfer is
    // abandoned without copying the invalid part and the status explains why.
    Status copy_from_user(void* kernel_destination, uintptr_t user_source, size_t length, size_t& copied);
    Status copy_to_user(uintptr_t user_destination, const void* kernel_source, size_t length, size_t& copied);

    // Reads a NUL-terminated string from user memory into a kernel buffer.
    // Returns Overflow when the string does not fit, which keeps a hostile
    // process from streaming bytes until the kernel overruns its buffer.
    Status copy_string_from_user(char* kernel_destination, size_t capacity, uintptr_t user_source, size_t& copied);

    uint64_t rejected_count();
    uint64_t copied_bytes();

    const char* status_name(Status status);
    bool validation_self_test();
}
