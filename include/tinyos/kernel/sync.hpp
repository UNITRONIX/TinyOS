#pragma once

#include <stdint.h>

namespace tinyos::kernel::sync
{
    // Interrupt flag save/restore. On a single-core machine a spinlock alone is
    // not enough: an IRQ that takes the same lock while it is already held
    // deadlocks forever. Every lock used from both task and interrupt context has
    // to disable interrupts for the duration of the critical section, which is
    // what IrqGuard provides.
    class IrqGuard
    {
    public:
        IrqGuard();
        ~IrqGuard();

        IrqGuard(const IrqGuard&) = delete;
        IrqGuard& operator=(const IrqGuard&) = delete;

        bool interrupts_were_enabled() const;

    private:
        bool g_interrupts_were_enabled;
    };

    // Test-and-set spinlock with a ticket-free simple form. Correct for the
    // current single-core kernel; the atomic instruction sequence is already
    // SMP-safe, so the same primitive works once application processors come up.
    class Spinlock
    {
    public:
        Spinlock();

        void lock();
        void unlock();
        bool try_lock();

        // Lock and disable interrupts. Prefer this over lock() for anything that
        // an interrupt handler can touch.
        void lock_irq(IrqGuard& guard);
        void unlock_irq(IrqGuard& guard);

    private:
        volatile int g_locked;
    };

    // Records how many nested critical sections are active. The scheduler uses it
    // to refuse a context switch from inside a critical section instead of
    // silently corrupting shared state.
    void enter_critical();
    void leave_critical();
    uint32_t critical_depth();

    // Statistics for diagnostics.
    uint64_t contention_count();
    uint64_t lock_order_violation_count();

    void initialize();
    bool is_ready();
    bool validation_self_test();
}
