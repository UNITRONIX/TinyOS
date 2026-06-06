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
        uintptr_t guard_page;
        uintptr_t kernel_stack_base;
        uintptr_t kernel_stack_top;
        size_t kernel_stack_size;
        uint64_t runtime_ticks;
        uint64_t wake_tick;
        uint64_t ticks_on_cpu;
        tinyos::arch::context::Context context;
        bool context_ready;
    };

    void initialize();
    bool install_stack_guards();
    Task* bootstrap_task();
    Task* idle_task();
    Task* sched_probe_task();
    const Task* task_at(size_t index);
    size_t task_count();
    size_t kernel_stack_bytes();
    size_t owned_kernel_stack_count();
    size_t prepared_context_count();
    size_t context_bytes();
    bool contexts_ready();
    bool guard_pages_ready();
    size_t guard_page_count();
    bool sched_probe_ran();
    bool guard_pages_validation_self_test();
    const char* state_name(State state);
}
