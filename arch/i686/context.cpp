#include <tinyos/arch/context.hpp>

extern "C" void arch_context_switch(tinyos::arch::context::Context* from, tinyos::arch::context::Context* to);

namespace
{
    uint32_t read_eflags()
    {
        uint32_t flags = 0;
        asm volatile ("pushfl; pop %0" : "=r"(flags));
        return flags;
    }

    uint32_t read_eip_marker()
    {
        uint32_t value = 0;
        asm volatile ("call 1f; 1: pop %0" : "=r"(value));
        return value;
    }

    uintptr_t align_down(uintptr_t value)
    {
        return value & ~(tinyos::arch::context::RequiredStackAlignment - 1);
    }
}

namespace tinyos::arch::context
{
    void clear(Context& context)
    {
        context.edi = 0;
        context.esi = 0;
        context.ebx = 0;
        context.ebp = 0;
        context.esp = 0;
        context.eip = 0;
        context.argument = 0;
        context.eflags = 0;
        context.magic = 0;
    }

    void capture_current(Context& context)
    {
        clear(context);

        uint32_t edi = 0;
        uint32_t esi = 0;
        uint32_t ebx = 0;
        uint32_t ebp = 0;
        uint32_t esp = 0;

        asm volatile (
            "mov %%edi, %0\n"
            "mov %%esi, %1\n"
            "mov %%ebx, %2\n"
            "mov %%ebp, %3\n"
            "mov %%esp, %4\n"
            : "=r"(edi), "=r"(esi), "=r"(ebx), "=r"(ebp), "=r"(esp)
            :
            : "memory");

        context.edi = edi;
        context.esi = esi;
        context.ebx = ebx;
        context.ebp = ebp;
        context.esp = esp;
        context.eip = read_eip_marker();
        context.argument = 0;
        context.eflags = read_eflags();
        context.magic = ContextMagic;
    }

    bool prepare_kernel_context(Context& context, uintptr_t stack_top, void (*entry)(void*), void* argument)
    {
        clear(context);

        if (stack_top == 0 || entry == nullptr)
        {
            return false;
        }

        const uintptr_t aligned_stack_top = align_down(stack_top);
        if (!stack_aligned(aligned_stack_top))
        {
            return false;
        }

        auto* stack_pointer = reinterpret_cast<uint32_t*>(aligned_stack_top);
        *--stack_pointer = reinterpret_cast<uint32_t>(argument);
        *--stack_pointer = 0;

        context.esp = reinterpret_cast<uint32_t>(stack_pointer);
        context.eip = reinterpret_cast<uint32_t>(entry);
        context.argument = reinterpret_cast<uint32_t>(argument);
        context.eflags = 0x00000202;
        context.magic = ContextMagic;
        return true;
    }

    bool is_valid(const Context& context)
    {
        return context.magic == ContextMagic &&
            context.esp != 0 &&
            context.eip != 0 &&
            stack_aligned(context.esp);
    }

    bool stack_aligned(uintptr_t stack_pointer)
    {
        return (stack_pointer % RequiredStackAlignment) == 0;
    }

    const char* abi_name()
    {
        return "i686-context-v0";
    }

    size_t context_size()
    {
        return sizeof(Context);
    }

    bool context_switch_available()
    {
        return true;
    }

    void switch_context(Context* from, Context* to)
    {
        if (to == nullptr || !is_valid(*to))
        {
            return;
        }

        arch_context_switch(from, to);
    }

    bool validation_self_test()
    {
        return context_switch_available();
    }
}