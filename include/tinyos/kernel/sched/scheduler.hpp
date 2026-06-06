#pragma once

#include <stddef.h>

#include <tinyos/kernel/task/task.hpp>

namespace tinyos::kernel::sched
{
    void initialize();
    bool is_ready();
    void on_timer_tick();
    void yield();
    void poll_reschedule();
    void sleep_ticks(uint64_t ticks);
    const tinyos::kernel::task::Task* current_task();
    uint64_t tick_count();
    uint64_t yield_count();
    uint64_t sleep_count();
    uint64_t wake_event_count();
    uint64_t context_switch_count();
    uint64_t dispatch_decision_count();
    uint64_t time_slice_ticks();
    size_t last_selected_task_id();
    bool preemption_enabled();
    bool round_robin_ready();
    bool sleep_wake_ready();
    size_t runnable_task_count();
    size_t blocked_task_count();
    size_t idle_task_count();
    bool validation_self_test();
    bool sleep_wake_validation_self_test();
    bool context_switch_validation_self_test();
    uint64_t watchdog_warning_count();
    uint64_t watchdog_threshold_ticks();
    uint64_t ticks_since_last_switch();
}
