#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/drivers/pit.hpp>

namespace
{
    tinyos::kernel::task::Task* g_current_task = nullptr;
    volatile uint64_t g_tick_count = 0;
    uint64_t g_yield_count = 0;
    uint64_t g_sleep_count = 0;
    uint64_t g_context_switch_count = 0;
    bool g_ready = false;
}

namespace tinyos::kernel::sched
{
    void initialize()
    {
        g_current_task = tinyos::kernel::task::bootstrap_task();
        g_tick_count = 0;
        g_yield_count = 0;
        g_sleep_count = 0;
        g_context_switch_count = 0;
        g_ready = g_current_task != nullptr;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Scheduler scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    void on_timer_tick()
    {
        if (!g_ready)
        {
            return;
        }

        ++g_tick_count;

        if (g_current_task != nullptr)
        {
            ++g_current_task->runtime_ticks;
        }
    }

    void yield()
    {
        ++g_yield_count;
    }

    void sleep_ticks(uint64_t ticks)
    {
        if (ticks == 0)
        {
            yield();
            return;
        }

        ++g_sleep_count;
        const uint64_t wake_tick = tinyos::drivers::pit::ticks() + ticks;
        if (g_current_task != nullptr)
        {
            g_current_task->wake_tick = wake_tick;
        }

        while (tinyos::drivers::pit::ticks() < wake_tick)
        {
            asm volatile ("hlt");
        }

        if (g_current_task != nullptr)
        {
            g_current_task->wake_tick = 0;
        }
    }

    const tinyos::kernel::task::Task* current_task()
    {
        return g_current_task;
    }

    uint64_t tick_count()
    {
        return g_tick_count;
    }

    uint64_t yield_count()
    {
        return g_yield_count;
    }

    uint64_t sleep_count()
    {
        return g_sleep_count;
    }

    uint64_t context_switch_count()
    {
        return g_context_switch_count;
    }

    bool preemption_enabled()
    {
        return false;
    }

    size_t runnable_task_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < tinyos::kernel::task::task_count(); ++index)
        {
            const auto* task = tinyos::kernel::task::task_at(index);
            if (task == nullptr)
            {
                continue;
            }

            if (task->state == tinyos::kernel::task::State::Ready || task->state == tinyos::kernel::task::State::Running)
            {
                ++count;
            }
        }

        return count;
    }

    size_t blocked_task_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < tinyos::kernel::task::task_count(); ++index)
        {
            const auto* task = tinyos::kernel::task::task_at(index);
            if (task != nullptr && task->state == tinyos::kernel::task::State::Blocked)
            {
                ++count;
            }
        }

        return count;
    }

    size_t idle_task_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < tinyos::kernel::task::task_count(); ++index)
        {
            const auto* task = tinyos::kernel::task::task_at(index);
            if (task != nullptr && task->state == tinyos::kernel::task::State::Idle)
            {
                ++count;
            }
        }

        return count;
    }
}
