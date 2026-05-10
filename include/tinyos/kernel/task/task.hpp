#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/arch/context.hpp>

namespace tinyos::kernel::task
{
    enum class State
    {
        Created,
        Ready,
        Running,
        Blocked,
        Idle
    };

    struct Task
    {
        size_t id;
        const char* name;
        State state;
        void (*entry)(void*);
        void* argument;
        uintptr_t kernel_stack_base;
        uintptr_t kernel_stack_top;
        size_t kernel_stack_size;
        uint64_t runtime_ticks;
        uint64_t wake_tick;
        tinyos::arch::context::Context context;
        bool context_ready;
    };

    void initialize();
    Task* bootstrap_task();
    Task* idle_task();
    const Task* task_at(size_t index);
    size_t task_count();
    size_t kernel_stack_bytes();
    size_t owned_kernel_stack_count();
    size_t prepared_context_count();
    size_t context_bytes();
    bool contexts_ready();
    const char* state_name(State state);
}
