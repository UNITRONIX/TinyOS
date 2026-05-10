#pragma once

#include <stddef.h>

#include <tinyos/kernel/task/task.hpp>

namespace tinyos::kernel::sched
{
    void initialize();
    bool is_ready();
    void on_timer_tick();
    void yield();
    void sleep_ticks(uint64_t ticks);
    const tinyos::kernel::task::Task* current_task();
    uint64_t tick_count();
    uint64_t yield_count();
    uint64_t sleep_count();
    uint64_t context_switch_count();
    bool preemption_enabled();
    size_t runnable_task_count();
    size_t blocked_task_count();
    size_t idle_task_count();
}
