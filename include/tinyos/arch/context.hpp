#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::arch::context
{
    inline constexpr uint32_t ContextMagic = 0x54435831;
    inline constexpr uintptr_t RequiredStackAlignment = 4;

    struct Context
    {
        uint32_t edi;
        uint32_t esi;
        uint32_t ebx;
        uint32_t ebp;
        uint32_t esp;
        uint32_t eip;
        uint32_t argument;
        uint32_t eflags;
        uint32_t magic;
    };

    void clear(Context& context);
    void capture_current(Context& context);
    bool prepare_kernel_context(Context& context, uintptr_t stack_top, void (*entry)(void*), void* argument);
    bool is_valid(const Context& context);
    bool stack_aligned(uintptr_t stack_pointer);
    const char* abi_name();
    size_t context_size();
    bool context_switch_available();
}