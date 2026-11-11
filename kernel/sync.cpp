#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/sync.hpp>

namespace
{
    uint32_t g_critical_depth = 0;
    uint64_t g_contention_count = 0;
    uint64_t g_lock_order_violation_count = 0;
    bool g_ready = false;

    bool interrupts_enabled()
    {
        uint32_t flags = 0;
        asm volatile ("pushfl; pop %0" : "=r"(flags));
        return (flags & 0x00000200u) != 0;
    }

    void disable_interrupts()
    {
        asm volatile ("cli" ::: "memory");
    }

    void enable_interrupts()
    {
        asm volatile ("sti" ::: "memory");
    }
}

namespace tinyos::kernel::sync
{
    IrqGuard::IrqGuard()
    {
        g_interrupts_were_enabled = interrupts_enabled();
        disable_interrupts();
        enter_critical();
    }

    IrqGuard::~IrqGuard()
    {
        leave_critical();
        if (g_interrupts_were_enabled)
        {
            enable_interrupts();
        }
    }

    bool IrqGuard::interrupts_were_enabled() const
    {
        return g_interrupts_were_enabled;
    }

    Spinlock::Spinlock()
    {
        g_locked = 0;
    }

    void Spinlock::lock()
    {
        // `xchg` is implicitly atomic on x86, so this is a correct test-and-set
        // without needing a separate barrier on the acquire path.
        for (;;)
        {
            int expected = 0;
            if (__atomic_compare_exchange_n(&g_locked, &expected, 1, false,
                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            {
                return;
            }

            ++g_contention_count;

            // Spin without hammering the cache line.
            while (__atomic_load_n(&g_locked, __ATOMIC_RELAXED) != 0)
            {
                asm volatile ("pause");
            }
        }
    }

    void Spinlock::unlock()
    {
        __atomic_store_n(&g_locked, 0, __ATOMIC_RELEASE);
    }

    bool Spinlock::try_lock()
    {
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_locked, &expected, 1, false,
                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            return true;
        }

        ++g_contention_count;
        return false;
    }

    void Spinlock::lock_irq(IrqGuard& guard)
    {
        (void)guard; // The guard disabled interrupts on construction.
        lock();

        // Taking a lock with interrupts already disabled at nesting depth > 0 is
        // fine; taking it while a critical section is active and interrupts are
        // enabled would be a latent deadlock. Record it rather than silently
        // accepting it.
        if (critical_depth() > 1 && interrupts_enabled())
        {
            ++g_lock_order_violation_count;
        }
    }

    void Spinlock::unlock_irq(IrqGuard& guard)
    {
        (void)guard;
        unlock();
    }

    void enter_critical()
    {
        ++g_critical_depth;
    }

    void leave_critical()
    {
        if (g_critical_depth > 0)
        {
            --g_critical_depth;
        }
    }

    uint32_t critical_depth()
    {
        return g_critical_depth;
    }

    uint64_t contention_count()
    {
        return g_contention_count;
    }

    uint64_t lock_order_violation_count()
    {
        return g_lock_order_violation_count;
    }

    void initialize()
    {
        g_critical_depth = 0;
        g_contention_count = 0;
        g_lock_order_violation_count = 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Kernel synchronization primitives ready.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        // Mutual exclusion: a second lock attempt by the same core must fail
        // while the first is held.
        Spinlock lock;
        lock.lock();
        const bool second_attempt_failed = !lock.try_lock();
        lock.unlock();
        const bool reacquired = lock.try_lock();
        if (reacquired)
        {
            lock.unlock();
        }

        // Interrupt state must be restored exactly, including the case where
        // interrupts were already disabled on entry.
        const bool enabled_before = interrupts_enabled();
        {
            IrqGuard guard;
            const bool disabled_inside = !interrupts_enabled();
            if (!disabled_inside)
            {
                return false;
            }
        }
        const bool restored = interrupts_enabled() == enabled_before;

        // Critical-section depth must not leak.
        const uint32_t depth_before = critical_depth();
        {
            IrqGuard guard;
            if (critical_depth() != depth_before + 1)
            {
                return false;
            }
        }
        const bool depth_restored = critical_depth() == depth_before;

        return second_attempt_failed && reacquired && restored && depth_restored;
    }
}
