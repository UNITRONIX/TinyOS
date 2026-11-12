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
    void irq_enter();
    void irq_leave();
    void irq_preempt_tail();
    void sleep_ticks(uint64_t ticks);

    // Marks the current task as waiting for an external event (keyboard, serial)
    // rather than executing.
    //
    // The preemption watchdog measures ticks since the last context switch, so a
    // task blocked in `hlt` waiting for input looked identical to a task stuck in
    // a loop. That produced a "Task watchdog: task exceeded time slice without
    // yield" warning every time the shell sat at the prompt. Entering an idle
    // wait excludes those ticks from the watchdog budget; leaving it restarts the
    // budget without counting the wait as a failure.
    void begin_idle_wait();
    void end_idle_wait();
    bool idle_wait_active();

    // Number of times a task entered an idle wait. Diagnostic only.
    uint64_t idle_wait_enter_count();
    const tinyos::kernel::task::Task* current_task();
    uint64_t tick_count();
    uint64_t yield_count();
    uint64_t sleep_count();
    uint64_t wake_event_count();
    uint64_t context_switch_count();
    uint64_t preempt_from_irq_count();
    uint64_t dispatch_decision_count();
    uint64_t time_slice_ticks();
    size_t last_selected_task_id();
    bool preemption_enabled();
    bool irq_preemption_active();
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
