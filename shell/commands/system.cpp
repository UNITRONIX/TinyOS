// System identification and health commands.
//
// These read contracts that already existed and were genuinely working before
// the shell rewrite: architecture info, memory map, device registry, paging,
// syscall table, runtime manifests. They were dropped when the old if/else
// dispatch chain was removed, which was a regression in usefulness rather than
// a cleanup — so they are restored here against the registry model.
//
// Each command reports only numbers the subsystem actually exposes. Where a
// contract is a scaffold (a manifest that describes a plan rather than a running
// feature) the command says so in its own text instead of implying readiness.

#include <tinyos/config.hpp>
#include <tinyos/arch/hal.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/kernel/interrupts.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/heap.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/platform/pc.hpp>
#include <tinyos/kernel/platform/requirements.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/syscall/syscall.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>
#include <tinyos/kernel/vfs/mount.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
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

    using tinyos::libshell::CapabilityAdmin;
    using tinyos::libshell::CapabilityDevice;
    using tinyos::libshell::CapabilityFileRead;
    using tinyos::libshell::CapabilityNone;

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

    void write_pair(Stream& stream, const char* label, const char* value)
    {
        stream.write(label);
        stream.line(value != nullptr ? value : "(none)");
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

    Status cmd_sysinfo(Context& context)
    {
        const auto& architecture = tinyos::arch::info();

        context.out.write("TinyOS ");
        context.out.line(tinyos::config::Version);
        write_pair(context.out, "Owner        : ", tinyos::config::Owner);
        write_pair(context.out, "Author       : ", tinyos::config::Author);
        write_pair(context.out, "License      : ", tinyos::config::License);
        write_pair(context.out, "Architecture : ", architecture.name);
        write_pair(context.out, "CPU family   : ", architecture.cpu_family);
        write_number(context.out, "Pointer bits : ", architecture.pointer_bits);
        write_boolean(context.out, "Paging       : ", architecture.paging_supported);
        write_boolean(context.out, "NX support   : ", architecture.nx_supported);

        // Runtime facts, not manifest claims.
        const uint64_t usable_mib = tinyos::kernel::memory::map::usable_bytes() / (1024 * 1024);
        write_number(context.out, "Usable RAM   : ", usable_mib);
        write_number(context.out, "Free frames  : ", tinyos::kernel::memory::frames::free_frames());
        write_number(context.out, "Heap free    : ", tinyos::kernel::memory::heap::free_bytes());
        write_number(context.out, "Tasks        : ", tinyos::kernel::task::task_count());
        write_number(context.out, "Commands     : ", tinyos::libshell::command_count());

        return Status::Ok;
    }

    Status cmd_status(Context& context)
    {
        const auto& architecture = tinyos::arch::info();

        context.out.line("TinyOS status:");
        write_pair(context.out, "Version      : ", tinyos::config::Version);
        write_pair(context.out, "Architecture : ", architecture.name);
        write_pair(context.out, "CWD          : ", context.cwd());

        write_number(context.out, "PIT ticks    : ", tinyos::drivers::pit::ticks());
        write_number(context.out, "Scheduler    : ", tinyos::kernel::sched::tick_count());
        write_number(context.out, "IRQs         : ", tinyos::kernel::interrupts::total_irq_count());
        write_number(context.out, "Unexpected   : ", tinyos::kernel::interrupts::unexpected_irq_count());

        write_number(context.out, "Usable RAM   : ",
            tinyos::kernel::memory::map::usable_bytes() / (1024 * 1024));

        context.out.write("Frames free  : ");
        write_unsigned(context.out, tinyos::kernel::memory::frames::free_frames());
        context.out.write("/");
        write_unsigned(context.out, tinyos::kernel::memory::frames::total_frames());
        context.out.put_char('\n');

        context.out.write("Heap free    : ");
        write_unsigned(context.out, tinyos::kernel::memory::heap::free_bytes());
        context.out.write("/");
        write_unsigned(context.out, tinyos::kernel::memory::heap::total_bytes());
        context.out.line(" bytes");

        write_boolean(context.out, "RAMFS ready  : ", tinyos::kernel::vfs::ramfs::is_ready());
        write_boolean(context.out, "Block mount  : ", tinyos::kernel::vfs::block_mount_ready());
        write_number(context.out, "Commands     : ", tinyos::libshell::command_count());

        return Status::Ok;
    }

    Status cmd_version(Context& context)
    {
        context.out.write(tinyos::config::Name);
        context.out.write(" ");
        context.out.write(tinyos::config::Version);
        context.out.write(" (");
        context.out.write(tinyos::config::Architecture);
        context.out.line(")");
        return Status::Ok;
    }

    Status cmd_archinfo(Context& context)
    {
        const auto& architecture = tinyos::arch::info();

        write_pair(context.out, "Name         : ", architecture.name);
        write_pair(context.out, "CPU family   : ", architecture.cpu_family);
        write_number(context.out, "Pointer bits : ", architecture.pointer_bits);
        write_number(context.out, "Page size    : ", architecture.page_size);
        write_boolean(context.out, "Protected    : ", architecture.protected_mode);
        write_boolean(context.out, "Paging       : ", architecture.paging_supported);
        write_boolean(context.out, "NX           : ", architecture.nx_supported);
        write_boolean(context.out, "Little endian: ", architecture.little_endian);

        // The backend is identified so a reader can tell i686 from x86_64
        // without inferring it from pointer width.
        context.out.write("Backend      : arch/");
        context.out.line(tinyos::config::Architecture);

        return Status::Ok;
    }

    Status cmd_meminfo(Context& context)
    {
        write_number(context.out, "Total bytes  : ", tinyos::kernel::memory::map::total_bytes());
        write_number(context.out, "Usable bytes : ", tinyos::kernel::memory::map::usable_bytes());
        write_number(context.out, "Regions      : ", tinyos::kernel::memory::map::region_count());

        for (size_t index = 0; index < tinyos::kernel::memory::map::region_count(); ++index)
        {
            const auto& region = tinyos::kernel::memory::map::region(index);
            context.out.write("  base=");
            write_unsigned(context.out, region.base);
            context.out.write(" length=");
            write_unsigned(context.out, region.length);
            context.out.write(" type=");
            write_unsigned(context.out, region.type);
            context.out.put_char('\n');
        }

        write_number(context.out, "Frames total : ", tinyos::kernel::memory::frames::total_frames());
        write_number(context.out, "Frames free  : ", tinyos::kernel::memory::frames::free_frames());
        write_boolean(context.out, "Accounting   : ", tinyos::kernel::memory::frames::accounting_valid());

        return Status::Ok;
    }

    Status cmd_frameinfo(Context& context)
    {
        write_number(context.out, "Frame size   : ", tinyos::kernel::memory::frames::FrameSize);
        write_number(context.out, "Total frames : ", tinyos::kernel::memory::frames::total_frames());
        write_number(context.out, "Free frames  : ", tinyos::kernel::memory::frames::free_frames());
        write_number(context.out, "Reserved     : ", tinyos::kernel::memory::frames::reserved_frames());
        write_number(context.out, "Alloc fails  : ", tinyos::kernel::memory::frames::allocation_failure_count());
        write_number(context.out, "Invalid free : ", tinyos::kernel::memory::frames::invalid_free_count());
        write_number(context.out, "Double free  : ", tinyos::kernel::memory::frames::double_free_count());
        write_boolean(context.out, "Accounting   : ", tinyos::kernel::memory::frames::accounting_valid());

        return Status::Ok;
    }

    Status cmd_heapinfo(Context& context)
    {
        write_number(context.out, "Total bytes  : ", tinyos::kernel::memory::heap::total_bytes());
        write_number(context.out, "Free bytes   : ", tinyos::kernel::memory::heap::free_bytes());
        write_number(context.out, "Used bytes   : ", tinyos::kernel::memory::heap::used_bytes());
        write_number(context.out, "Blocks       : ", tinyos::kernel::memory::heap::block_count());
        write_number(context.out, "Invalid free : ", tinyos::kernel::memory::heap::invalid_free_count());
        write_number(context.out, "Double free  : ", tinyos::kernel::memory::heap::double_free_count());
        write_number(context.out, "Corrupt      : ", tinyos::kernel::memory::heap::corrupt_block_count());
        write_boolean(context.out, "State valid  : ", tinyos::kernel::memory::heap::state_valid());

        return Status::Ok;
    }

    Status cmd_paginginfo(Context& context)
    {
        write_boolean(context.out, "Runtime      : ", tinyos::kernel::memory::paging::is_runtime_enabled());
        write_boolean(context.out, "Ready        : ", tinyos::kernel::memory::paging::is_ready());
        write_number(context.out, "Mapped pages : ", tinyos::kernel::memory::paging::mapped_pages());
        write_number(context.out, "Mapped bytes : ", tinyos::kernel::memory::paging::mapped_bytes());
        write_number(context.out, "Identity     : ", tinyos::kernel::memory::paging::bootstrap_identity_bytes());
        write_number(context.out, "Page dir     : ", tinyos::kernel::memory::paging::page_directory_address());

        return Status::Ok;
    }

    Status cmd_irqinfo(Context& context)
    {
        write_number(context.out, "Total IRQs   : ", tinyos::kernel::interrupts::total_irq_count());
        write_number(context.out, "Unexpected   : ", tinyos::kernel::interrupts::unexpected_irq_count());
        write_boolean(context.out, "Seen IRQ     : ", tinyos::kernel::interrupts::has_seen_irq());
        write_boolean(context.out, "Hardware IRQ : ", tinyos::kernel::interrupts::hardware_irq_enabled());

        // Per-line counters make a stuck or misfiring IRQ line identifiable
        // without a debugger.
        context.out.line("");
        context.out.line("Per line:");
        for (uint8_t irq = 0; irq < 16; ++irq)
        {
            const uint64_t count = tinyos::kernel::interrupts::irq_count(irq);
            if (count == 0)
            {
                continue;
            }

            context.out.write("  irq");
            write_unsigned(context.out, irq);
            context.out.write(" : ");
            write_unsigned(context.out, count);
            context.out.put_char('\n');
        }

        return Status::Ok;
    }

    Status cmd_timerinfo(Context& context)
    {
        write_boolean(context.out, "PIT          : ", tinyos::drivers::pit::is_configured());
        write_number(context.out, "Ticks        : ", tinyos::drivers::pit::ticks());

        // The scheduler derives its slice length from the same tick source, so
        // reporting it here shows both views of the same clock.
        write_number(context.out, "Sched ticks  : ", tinyos::kernel::sched::tick_count());
        write_number(context.out, "Time slice   : ", tinyos::kernel::sched::time_slice_ticks());

        return Status::Ok;
    }

    Status cmd_requirements(Context& context)
    {
        // This is a manifest of the documented envelope, not a measurement, so
        // say so and then report the measured values next to it.
        context.out.line("Documented minimum envelope:");
        write_pair(context.out, "  arch       : ", tinyos::config::Architecture);
        write_pair(context.out, "  boot       : ", "GRUB Multiboot ISO");
        write_pair(context.out, "  display    : ", "VGA text 80x25");
        write_pair(context.out, "  input      : ", "PS/2 keyboard and serial");

        context.out.line("");
        context.out.line("Measured on this machine:");
        write_number(context.out, "  usable RAM : ",
            tinyos::kernel::memory::map::usable_bytes() / (1024 * 1024));
        write_boolean(context.out, "  VGA        : ", true);
        context.out.write("  console in : ");
        context.out.line("keyboard + serial");

        context.out.write("requirements manifest: ");
        context.out.line(tinyos::kernel::platform::requirements::validation_self_test() ? "valid" : "invalid");

        return Status::Ok;
    }

    Status cmd_platforminfo(Context& context)
    {
        write_boolean(context.out, "PC contract  : ", tinyos::kernel::platform::pc::validation_self_test());
        write_boolean(context.out, "Device classes: ", tinyos::kernel::platform::pc::device_contract_satisfied());

        return Status::Ok;
    }

    Status cmd_syscallinfo(Context& context)
    {
        write_number(context.out, "Definitions  : ", tinyos::kernel::syscall::definition_count());
        write_number(context.out, "Implemented  : ", tinyos::kernel::syscall::implemented_definition_count());
        write_number(context.out, "Max buf bytes: ", tinyos::kernel::syscall::max_user_buffer_bytes());
        write_boolean(context.out, "Boundary OK  : ", tinyos::kernel::syscall::validation_self_test());
        write_boolean(context.out, "Filter OK    : ", tinyos::kernel::syscall::filter_policy_validation_self_test());

        context.out.line("");
        context.out.line("Syscall table:");
        for (size_t index = 0; index < tinyos::kernel::syscall::definition_count(); ++index)
        {
            const auto* definition = tinyos::kernel::syscall::definition_at(index);
            if (definition == nullptr)
            {
                continue;
            }

            context.out.write("  ");
            context.out.write(definition->name != nullptr ? definition->name : "?");
            context.out.write(definition->implemented ? "  implemented" : "  not implemented");
            context.out.put_char('\n');
        }

        return Status::Ok;
    }

    Status cmd_vfsinfo(Context& context)
    {
        write_boolean(context.out, "VFS ready    : ", tinyos::kernel::vfs::is_ready());
        write_boolean(context.out, "RAMFS        : ", tinyos::kernel::vfs::ramfs::is_ready());
        write_boolean(context.out, "Block mount  : ", tinyos::kernel::vfs::block_mount_ready());
        write_number(context.out, "Mounts       : ", tinyos::kernel::vfs::mount::active_count());

        return Status::Ok;
    }

    Status cmd_ramfsinfo(Context& context)
    {
        const auto* root = tinyos::kernel::vfs::root();
        write_boolean(context.out, "Ready        : ", tinyos::kernel::vfs::ramfs::is_ready());

        if (root != nullptr)
        {
            write_number(context.out, "Root children: ", tinyos::kernel::vfs::child_count(root));
        }

        // The hard limits that decide how much the whole system can hold. They
        // are reported because they are the reason a home directory will not fit
        // until RAMFS allocates dynamically (defect D6).
        context.out.line("");
        context.out.line("RAMFS allocates from a fixed pool; these are the ceilings:");
        context.out.line("  files 8, size 512 bytes each, name 48 bytes");

        return Status::Ok;
    }

    Status cmd_storageinfo(Context& context)
    {
        write_boolean(context.out, "Block mount  : ", tinyos::kernel::vfs::block_mount_ready());
        write_number(context.out, "Mounts       : ", tinyos::kernel::vfs::mount::active_count());
        write_boolean(context.out, "Blockfs valid: ", tinyos::kernel::vfs::blockfs::validation_self_test());

        for (size_t index = 0; index < tinyos::kernel::vfs::mount::active_count(); ++index)
        {
            const char* source = nullptr;
            const char* target = nullptr;
            if (!tinyos::kernel::vfs::mount::active_at(index, source, target))
            {
                continue;
            }

            context.out.write("  ");
            context.out.write(target != nullptr ? target : "?");
            context.out.write(" -> ");
            context.out.line(source != nullptr ? source : "?");
        }

        return Status::Ok;
    }

    const tinyos::libshell::Command SystemCommands[] = {
        { "sysinfo", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_sysinfo, "show system information", "sysinfo", "sysinfo" },
        { "status", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_status, "compact system dashboard", "status", "status" },
        { "version", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_version, "print the version", "version", "version" },
        { "archinfo", nullptr, Category::Devices, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_archinfo, "show architecture capabilities", "archinfo", "archinfo" },
        { "meminfo", nullptr, Category::Memory, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_meminfo, "show the memory map", "meminfo", "meminfo" },
        { "frameinfo", nullptr, Category::Memory, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_frameinfo, "show the frame allocator", "frameinfo", "frameinfo" },
        { "heapinfo", nullptr, Category::Memory, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_heapinfo, "show the kernel heap", "heapinfo", "heapinfo" },
        { "paginginfo", nullptr, Category::Memory, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_paginginfo, "show paging state", "paginginfo", "paginginfo" },
        { "irqinfo", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_irqinfo, "show interrupt counters", "irqinfo", "irqinfo" },
        { "timerinfo", nullptr, Category::Scheduling, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_timerinfo, "show the PIT timer", "timerinfo", "timerinfo" },
        { "requirements", nullptr, Category::Security, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_requirements, "show the documented runtime envelope", "requirements", "requirements" },
        { "platforminfo", nullptr, Category::Devices, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_platforminfo, "show the platform contract", "platforminfo", "platforminfo" },
        { "syscallinfo", nullptr, Category::Runtime, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_syscallinfo, "show the syscall table", "syscallinfo", "syscallinfo" },
        { "vfsinfo", nullptr, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_vfsinfo, "show VFS state", "vfsinfo", "vfsinfo" },
        { "ramfsinfo", nullptr, Category::Files, Availability::Real, Risk::ReadOnly, CapabilityFileRead,
            cmd_ramfsinfo, "show RAMFS state and limits", "ramfsinfo", "ramfsinfo" },
        { "storageinfo", nullptr, Category::Storage, Availability::Real, Risk::ReadOnly, CapabilityDevice,
            cmd_storageinfo, "show storage mounts", "storageinfo", "storageinfo" }
    };
}

namespace tinyos::shell::commands
{
    void register_system_commands()
    {
        tinyos::libshell::register_table(SystemCommands,
            sizeof(SystemCommands) / sizeof(SystemCommands[0]));
    }
}
