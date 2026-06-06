#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/task/task.hpp>

namespace
{
    constexpr size_t GuardPageSize = 4096;
    constexpr size_t UsableStackSize = 4096;

    alignas(4096) unsigned char g_bootstrap_stack[GuardPageSize + UsableStackSize] = {};
    alignas(4096) unsigned char g_idle_stack[GuardPageSize + UsableStackSize] = {};
    alignas(4096) unsigned char g_probe_stack[GuardPageSize + UsableStackSize] = {};
    tinyos::kernel::task::Task g_tasks[3] = {};
    size_t g_task_count = 0;
    bool g_probe_ran = false;
    size_t g_guard_page_count = 0;

    uintptr_t guard_page_for(unsigned char* stack)
    {
        return reinterpret_cast<uintptr_t>(&stack[0]);
    }

    uintptr_t stack_base(unsigned char* stack)
    {
        return reinterpret_cast<uintptr_t>(&stack[GuardPageSize]);
    }

    uintptr_t stack_top(unsigned char* stack)
    {
        return reinterpret_cast<uintptr_t>(&stack[GuardPageSize + UsableStackSize]);
    }

    void idle_task_entry(void*)
    {
        for (;;)
        {
            tinyos::kernel::sched::poll_reschedule();
            asm volatile ("hlt");
        }
    }
}

extern "C" void tinyos_sched_probe_entry(void*)
{
    g_probe_ran = true;
    tinyos::kernel::sched::yield();
    for (;;)
    {
        tinyos::kernel::sched::poll_reschedule();
        asm volatile ("hlt");
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
        g_tasks[0].guard_page = guard_page_for(g_bootstrap_stack);
        g_tasks[0].kernel_stack_base = stack_base(g_bootstrap_stack);
        g_tasks[0].kernel_stack_top = stack_top(g_bootstrap_stack);
        g_tasks[0].kernel_stack_size = UsableStackSize;
        g_tasks[0].runtime_ticks = 0;
        g_tasks[0].wake_tick = 0;
        g_tasks[0].ticks_on_cpu = 0;
        tinyos::arch::context::capture_current(g_tasks[0].context);
        g_tasks[0].context_ready = tinyos::arch::context::is_valid(g_tasks[0].context);

        g_tasks[1].id = 1;
        g_tasks[1].name = "idle";
        g_tasks[1].state = State::Idle;
        g_tasks[1].entry = idle_task_entry;
        g_tasks[1].argument = nullptr;
        g_tasks[1].guard_page = guard_page_for(g_idle_stack);
        g_tasks[1].kernel_stack_base = stack_base(g_idle_stack);
        g_tasks[1].kernel_stack_top = stack_top(g_idle_stack);
        g_tasks[1].kernel_stack_size = UsableStackSize;
        g_tasks[1].runtime_ticks = 0;
        g_tasks[1].wake_tick = 0;
        g_tasks[1].ticks_on_cpu = 0;
        g_tasks[1].context_ready = tinyos::arch::context::prepare_kernel_context(
            g_tasks[1].context,
            g_tasks[1].kernel_stack_top,
            g_tasks[1].entry,
            g_tasks[1].argument);

        g_tasks[2].id = 2;
        g_tasks[2].name = "sched-probe";
        g_tasks[2].state = State::Ready;
        g_tasks[2].entry = tinyos_sched_probe_entry;
        g_tasks[2].argument = nullptr;
        g_tasks[2].guard_page = guard_page_for(g_probe_stack);
        g_tasks[2].kernel_stack_base = stack_base(g_probe_stack);
        g_tasks[2].kernel_stack_top = stack_top(g_probe_stack);
        g_tasks[2].kernel_stack_size = UsableStackSize;
        g_tasks[2].runtime_ticks = 0;
        g_tasks[2].wake_tick = 0;
        g_tasks[2].ticks_on_cpu = 0;
        g_tasks[2].context_ready = tinyos::arch::context::prepare_kernel_context(
            g_tasks[2].context,
            g_tasks[2].kernel_stack_top,
            g_tasks[2].entry,
            g_tasks[2].argument);

        g_task_count = 3;
        g_probe_ran = false;
        g_guard_page_count = 0;
    }

    bool install_stack_guards()
    {
        if (!memory::paging::is_runtime_enabled())
        {
            return false;
        }

        g_guard_page_count = 0;
        for (size_t index = 0; index < g_task_count; ++index)
        {
            if (g_tasks[index].guard_page == 0)
            {
                continue;
            }

            if (memory::paging::clear_page_present(g_tasks[index].guard_page))
            {
                ++g_guard_page_count;
            }
        }

        return g_guard_page_count == g_task_count;
    }

    Task* bootstrap_task()
    {
        return &g_tasks[0];
    }

    Task* idle_task()
    {
        return &g_tasks[1];
    }

    Task* sched_probe_task()
    {
        return &g_tasks[2];
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

    bool guard_pages_ready()
    {
        return g_guard_page_count == g_task_count && g_task_count > 0;
    }

    size_t guard_page_count()
    {
        return g_guard_page_count;
    }

    bool sched_probe_ran()
    {
        return g_probe_ran;
    }

    bool guard_pages_validation_self_test()
    {
        if (!guard_pages_ready())
        {
            return false;
        }

        for (size_t index = 0; index < g_task_count; ++index)
        {
            memory::paging::PageMapping mapping;
            if (memory::paging::mapping_for(g_tasks[index].guard_page, mapping))
            {
                return false;
            }
        }

        return true;
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
