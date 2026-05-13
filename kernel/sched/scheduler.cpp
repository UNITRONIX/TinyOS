#include <tinyos/arch/context.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/drivers/pit.hpp>

namespace
{
    constexpr uint64_t TimeSliceTicks = 5;

    tinyos::kernel::task::Task* g_current_task = nullptr;
    tinyos::kernel::task::Task* g_last_selected_task = nullptr;
    volatile uint64_t g_tick_count = 0;
    uint64_t g_yield_count = 0;
    uint64_t g_sleep_count = 0;
    uint64_t g_wake_event_count = 0;
    uint64_t g_context_switch_count = 0;
    uint64_t g_dispatch_decision_count = 0;
    bool g_ready = false;

    bool task_is_runnable(const tinyos::kernel::task::Task* task)
    {
        return task != nullptr &&
            (task->state == tinyos::kernel::task::State::Ready || task->state == tinyos::kernel::task::State::Running);
    }

    tinyos::kernel::task::Task* mutable_task_at(size_t index)
    {
        if (index == 0)
        {
            return tinyos::kernel::task::bootstrap_task();
        }

        if (index == 1)
        {
            return tinyos::kernel::task::idle_task();
        }

        return nullptr;
    }

    size_t task_index(const tinyos::kernel::task::Task* task)
    {
        if (task == nullptr)
        {
            return 0;
        }

        return task->id < tinyos::kernel::task::task_count() ? task->id : 0;
    }

    tinyos::kernel::task::Task* select_next_task()
    {
        const size_t count = tinyos::kernel::task::task_count();
        if (count == 0)
        {
            return nullptr;
        }

        const size_t start = (task_index(g_last_selected_task != nullptr ? g_last_selected_task : g_current_task) + 1) % count;
        for (size_t offset = 0; offset < count; ++offset)
        {
            const size_t index = (start + offset) % count;
            auto* task = mutable_task_at(index);
            if (task_is_runnable(task))
            {
                return task;
            }
        }

        return tinyos::kernel::task::idle_task();
    }

    void record_dispatch_decision()
    {
        auto* selected = select_next_task();
        if (selected == nullptr)
        {
            return;
        }

        g_last_selected_task = selected;
        ++g_dispatch_decision_count;

        if (selected != g_current_task && tinyos::arch::context::context_switch_available())
        {
            ++g_context_switch_count;
            g_current_task = selected;
        }
    }

    void wake_blocked_tasks(uint64_t now)
    {
        for (size_t index = 0; index < tinyos::kernel::task::task_count(); ++index)
        {
            auto* task = mutable_task_at(index);
            if (task == nullptr || task->state != tinyos::kernel::task::State::Blocked || task->wake_tick == 0)
            {
                continue;
            }

            if (task->wake_tick <= now)
            {
                task->wake_tick = 0;
                task->state = tinyos::kernel::task::State::Ready;
                ++g_wake_event_count;
            }
        }
    }
}

namespace tinyos::kernel::sched
{
    void initialize()
    {
        g_current_task = tinyos::kernel::task::bootstrap_task();
        g_last_selected_task = g_current_task;
        g_tick_count = 0;
        g_yield_count = 0;
        g_sleep_count = 0;
        g_wake_event_count = 0;
        g_context_switch_count = 0;
        g_dispatch_decision_count = 0;
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

        wake_blocked_tasks(g_tick_count);

        if ((g_tick_count % TimeSliceTicks) == 0)
        {
            record_dispatch_decision();
        }
    }

    void yield()
    {
        ++g_yield_count;
        if (g_ready)
        {
            record_dispatch_decision();
        }
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
        tinyos::kernel::task::State previous_state = tinyos::kernel::task::State::Running;
        if (g_current_task != nullptr)
        {
            previous_state = g_current_task->state;
            g_current_task->state = tinyos::kernel::task::State::Blocked;
            g_current_task->wake_tick = wake_tick;
            record_dispatch_decision();
        }

        while (tinyos::drivers::pit::ticks() < wake_tick)
        {
            asm volatile ("hlt");
        }

        if (g_current_task != nullptr)
        {
            g_current_task->wake_tick = 0;
            if (g_current_task->state == tinyos::kernel::task::State::Ready || g_current_task->state == tinyos::kernel::task::State::Blocked)
            {
                g_current_task->state = previous_state;
            }
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

    uint64_t wake_event_count()
    {
        return g_wake_event_count;
    }

    uint64_t context_switch_count()
    {
        return g_context_switch_count;
    }

    uint64_t dispatch_decision_count()
    {
        return g_dispatch_decision_count;
    }

    uint64_t time_slice_ticks()
    {
        return TimeSliceTicks;
    }

    size_t last_selected_task_id()
    {
        return g_last_selected_task != nullptr ? g_last_selected_task->id : 0;
    }

    bool preemption_enabled()
    {
        return tinyos::arch::context::context_switch_available();
    }

    bool round_robin_ready()
    {
        return g_ready &&
            TimeSliceTicks != 0 &&
            tinyos::kernel::task::task_count() >= 2 &&
            g_current_task == tinyos::kernel::task::bootstrap_task() &&
            g_last_selected_task != nullptr &&
            runnable_task_count() >= 1 &&
            idle_task_count() == 1;
    }

    bool sleep_wake_ready()
    {
        return round_robin_ready() &&
            g_current_task != nullptr &&
            g_current_task->wake_tick == 0 &&
            blocked_task_count() == 0;
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

            if (task_is_runnable(task))
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

    bool validation_self_test()
    {
        if (!round_robin_ready())
        {
            return false;
        }

        const auto* selected = select_next_task();
        return selected != nullptr &&
            task_is_runnable(selected) &&
            time_slice_ticks() == TimeSliceTicks &&
            dispatch_decision_count() <= tick_count() + yield_count() + sleep_count() &&
            blocked_task_count() == 0 &&
            context_switch_count() == 0 &&
            !preemption_enabled();
    }

    bool sleep_wake_validation_self_test()
    {
        if (!sleep_wake_ready())
        {
            return false;
        }

        auto* task = tinyos::kernel::task::bootstrap_task();
        if (task == nullptr)
        {
            return false;
        }

        const auto saved_state = task->state;
        const uint64_t saved_wake_tick = task->wake_tick;
        const uint64_t saved_wake_count = g_wake_event_count;
        task->state = tinyos::kernel::task::State::Blocked;
        task->wake_tick = g_tick_count + 1;

        wake_blocked_tasks(g_tick_count);
        const bool not_early = task->state == tinyos::kernel::task::State::Blocked && task->wake_tick == g_tick_count + 1;
        wake_blocked_tasks(g_tick_count + 1);
        const bool woke = task->state == tinyos::kernel::task::State::Ready && task->wake_tick == 0 && g_wake_event_count == saved_wake_count + 1;

        task->state = saved_state;
        task->wake_tick = saved_wake_tick;
        g_wake_event_count = saved_wake_count;

        return not_early && woke;
    }
}
