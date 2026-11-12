// Scheduler and task diagnostics.
//
// Split from system.cpp so each module covers one subsystem. These read the
// scheduler and task tables that already report real numbers.

#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/shell/commands/commands.hpp>

namespace
{
    using tinyos::libshell::Availability;
    using tinyos::libshell::Category;
    using tinyos::libshell::Context;
    using tinyos::libshell::Risk;
    using tinyos::libshell::Status;
    using tinyos::libshell::Stream;

    using tinyos::libshell::CapabilityNone;
    using tinyos::libshell::CapabilityProcess;

    void write_unsigned(Stream& stream, uint64_t value)
    {
        char digits[21];
        size_t position = sizeof(digits);
        digits[--position] = '\0';

        do
        {
            digits[--position] = static_cast<char>('0' + (value % 10));
            value /= 10;
        } while (value != 0 && position > 0);

        stream.write(&digits[position]);
    }

    void write_number(Stream& stream, const char* label, uint64_t value)
    {
        stream.write(label);
        write_unsigned(stream, value);
        stream.put_char('\n');
    }

    void write_boolean(Stream& stream, const char* label, bool value)
    {
        stream.write(label);
        stream.line(value ? "yes" : "no");
    }

    Status cmd_ps(Context& context)
    {
        // Columns are fixed width because the console is 80 columns and a
        // variable layout wraps unpredictably.
        context.out.line("PID  STATE      TICKS  NAME");

        for (size_t index = 0; index < tinyos::kernel::task::task_count(); ++index)
        {
            const auto* task = tinyos::kernel::task::task_at(index);
            if (task == nullptr)
            {
                continue;
            }

            write_unsigned(context.out, task->id);

            const char* state = tinyos::kernel::task::state_name(task->state);
            const size_t state_length = tinyos::libk::text::length(state);
            for (size_t pad = 0; pad < 5; ++pad)
            {
                context.out.put_char(' ');
            }
            context.out.write(state);
            for (size_t pad = state_length; pad < 9; ++pad)
            {
                context.out.put_char(' ');
            }

            write_unsigned(context.out, task->runtime_ticks);
            context.out.write("    ");
            context.out.line(task->name != nullptr ? task->name : "unnamed");
        }

        return Status::Ok;
    }

    Status cmd_schedinfo(Context& context)
    {
        write_boolean(context.out, "Ready        : ", tinyos::kernel::sched::is_ready());
        write_boolean(context.out, "Preemption   : ", tinyos::kernel::sched::irq_preemption_active());
        write_boolean(context.out, "Round robin  : ", tinyos::kernel::sched::round_robin_ready());
        write_boolean(context.out, "Sleep/wake   : ", tinyos::kernel::sched::sleep_wake_ready());

        context.out.line("");
        write_number(context.out, "Run queue    : ", tinyos::kernel::sched::runnable_task_count());
        write_number(context.out, "Blocked      : ", tinyos::kernel::sched::blocked_task_count());
        write_number(context.out, "Idle         : ", tinyos::kernel::sched::idle_task_count());
        write_number(context.out, "Time slice   : ", tinyos::kernel::sched::time_slice_ticks());

        context.out.line("");
        write_number(context.out, "Switches     : ", tinyos::kernel::sched::context_switch_count());
        write_number(context.out, "Preemptions  : ", tinyos::kernel::sched::preempt_from_irq_count());
        write_number(context.out, "Yields       : ", tinyos::kernel::sched::yield_count());
        write_number(context.out, "Sleeps       : ", tinyos::kernel::sched::sleep_count());
        write_number(context.out, "Wake events  : ", tinyos::kernel::sched::wake_event_count());

        // Watchdog state is reported because a non-zero count means a task ran
        // far past its slice, which is exactly the class of bug that hangs a
        // system silently.
        write_number(context.out, "Watchdog     : ", tinyos::kernel::sched::watchdog_warning_count());
        write_number(context.out, "Since switch : ", tinyos::kernel::sched::ticks_since_last_switch());

        return Status::Ok;
    }

    Status cmd_taskinfo(Context& context)
    {
        write_number(context.out, "Tasks        : ", tinyos::kernel::task::task_count());
        write_number(context.out, "Stacks owned : ", tinyos::kernel::task::owned_kernel_stack_count());
        write_number(context.out, "Stack bytes  : ", tinyos::kernel::task::kernel_stack_bytes());
        write_number(context.out, "Guard pages  : ", tinyos::kernel::task::guard_page_count());
        write_number(context.out, "Context ready: ", tinyos::kernel::task::prepared_context_count());
        write_number(context.out, "Context bytes: ", tinyos::kernel::task::context_bytes());

        write_boolean(context.out, "Contexts ok  : ", tinyos::kernel::task::contexts_ready());
        write_boolean(context.out, "Guards ok    : ", tinyos::kernel::task::guard_pages_ready());

        // The 3-task table and 4 KiB stacks are the reason this cannot hold a
        // real userland yet; naming them here keeps that visible (defect D7).
        context.out.line("");
        context.out.line("Limits: fixed table of 3 tasks, 4 KiB kernel stacks.");

        return Status::Ok;
    }

    Status cmd_uptime(Context& context)
    {
        const uint64_t ticks = tinyos::kernel::sched::tick_count();

        // The PIT runs at a fixed rate; ticks map to centiseconds at 100 Hz.
        write_number(context.out, "Ticks        : ", ticks);
        write_number(context.out, "Seconds      : ", ticks / 100);
        write_number(context.out, "Centiseconds : ", ticks % 100);

        return Status::Ok;
    }

    Status cmd_yield(Context& context)
    {
        // Yielding from a shell command is a real scheduler call, not a stub: it
        // hands the CPU to the next runnable task and returns.
        tinyos::kernel::sched::yield();
        context.out.line("yielded");
        return Status::Ok;
    }

    const tinyos::libshell::Command SchedulerCommands[] = {
        { "ps", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_ps, "list kernel tasks", "ps", "ps" },
        { "schedinfo", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_schedinfo, "show scheduler state", "schedinfo", "schedinfo" },
        { "taskinfo", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_taskinfo, "show task bookkeeping", "taskinfo", "taskinfo" },
        { "uptime", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_uptime, "show ticks since boot", "uptime", "uptime" },
        { "yield", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityProcess,
            cmd_yield, "yield the CPU once", "yield", "yield" }
    };
}

namespace tinyos::shell::commands
{
    void register_scheduler_commands()
    {
        tinyos::libshell::register_table(SchedulerCommands,
            sizeof(SchedulerCommands) / sizeof(SchedulerCommands[0]));
    }
}
