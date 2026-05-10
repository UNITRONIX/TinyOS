#include <tinyos/kernel/task/task.hpp>

namespace
{
    constexpr size_t KernelStackSize = 4096;

    alignas(16) unsigned char g_bootstrap_stack[KernelStackSize] = {};
    alignas(16) unsigned char g_idle_stack[KernelStackSize] = {};
    tinyos::kernel::task::Task g_tasks[2] = {};
    size_t g_task_count = 0;

    uintptr_t stack_base(unsigned char* stack)
    {
        return reinterpret_cast<uintptr_t>(&stack[0]);
    }

    uintptr_t stack_top(unsigned char* stack)
    {
        return reinterpret_cast<uintptr_t>(&stack[KernelStackSize]);
    }

    void idle_task_entry(void*)
    {
        for (;;)
        {
            asm volatile ("hlt");
        }
    }
}

namespace tinyos::kernel::task
{
    void initialize()
    {
        g_tasks[0].id = 0;
        g_tasks[0].name = "bootstrap";
        g_tasks[0].state = State::Running;
        g_tasks[0].entry = nullptr;
        g_tasks[0].argument = nullptr;
        g_tasks[0].kernel_stack_base = stack_base(g_bootstrap_stack);
        g_tasks[0].kernel_stack_top = stack_top(g_bootstrap_stack);
        g_tasks[0].kernel_stack_size = KernelStackSize;
        g_tasks[0].runtime_ticks = 0;
        g_tasks[0].wake_tick = 0;
        tinyos::arch::context::capture_current(g_tasks[0].context);
        g_tasks[0].context_ready = tinyos::arch::context::is_valid(g_tasks[0].context);

        g_tasks[1].id = 1;
        g_tasks[1].name = "idle";
        g_tasks[1].state = State::Idle;
        g_tasks[1].entry = idle_task_entry;
        g_tasks[1].argument = nullptr;
        g_tasks[1].kernel_stack_base = stack_base(g_idle_stack);
        g_tasks[1].kernel_stack_top = stack_top(g_idle_stack);
        g_tasks[1].kernel_stack_size = KernelStackSize;
        g_tasks[1].runtime_ticks = 0;
        g_tasks[1].wake_tick = 0;
        g_tasks[1].context_ready = tinyos::arch::context::prepare_kernel_context(
            g_tasks[1].context,
            g_tasks[1].kernel_stack_top,
            g_tasks[1].entry,
            g_tasks[1].argument);

        g_task_count = 2;
    }

    Task* bootstrap_task()
    {
        return &g_tasks[0];
    }

    Task* idle_task()
    {
        return &g_tasks[1];
    }

    const Task* task_at(size_t index)
    {
        if (index >= g_task_count)
        {
            return nullptr;
        }

        return &g_tasks[index];
    }

    size_t task_count()
    {
        return g_task_count;
    }

    size_t kernel_stack_bytes()
    {
        size_t total = 0;
        for (size_t index = 0; index < g_task_count; ++index)
        {
            total += g_tasks[index].kernel_stack_size;
        }

        return total;
    }

    size_t owned_kernel_stack_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < g_task_count; ++index)
        {
            if (g_tasks[index].kernel_stack_base != 0 && g_tasks[index].kernel_stack_top != 0 && g_tasks[index].kernel_stack_size != 0)
            {
                ++count;
            }
        }

        return count;
    }

    size_t prepared_context_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < g_task_count; ++index)
        {
            if (g_tasks[index].context_ready && tinyos::arch::context::is_valid(g_tasks[index].context))
            {
                ++count;
            }
        }

        return count;
    }

    size_t context_bytes()
    {
        return g_task_count * tinyos::arch::context::context_size();
    }

    bool contexts_ready()
    {
        return g_task_count > 0 && prepared_context_count() == g_task_count;
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Created:
            return "created";
        case State::Ready:
            return "ready";
        case State::Running:
            return "running";
        case State::Blocked:
            return "blocked";
        case State::Idle:
            return "idle";
        }

        return "unknown";
    }
}
