// Commands that are declared but not implemented yet.
//
// These exist so the shell tells the truth. Previously the tool manifest in
// kernel/admin/tools.cpp listed about forty commands as State::Ready while they
// either printed "not wired to a create path yet" or did not exist at all, and
// the shell reported a missing command as "not found" — which reads as a typo,
// not as "this feature is not built" (defects D21, D2).
//
// Every entry here is registered with the real blocker in its Availability, so
// running it produces:
//
//     netconfig: not available yet (needs-networking)
//
// That is a useful answer. `help` and `tools` also list the command and its
// status, so the command surface is discoverable without pretending.

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

    // Capability is a plain enum, so its enumerators are namespace-scoped.
    using tinyos::libshell::CapabilityAdmin;
    using tinyos::libshell::CapabilityFileRead;
    using tinyos::libshell::CapabilityFileWrite;
    using tinyos::libshell::CapabilityNetwork;
    using tinyos::libshell::CapabilityNone;
    using tinyos::libshell::CapabilityProcess;

    // The registry validation rejects a null handler, so unimplemented commands
    // carry this instead of nullptr. It is unreachable through normal dispatch
    // because the execution engine checks availability first, but returning a
    // distinct status keeps it honest if that ever changes.
    Status not_implemented(Context& context)
    {
        context.err.line("not implemented");
        return Status::Unavailable;
    }

    const tinyos::libshell::Command PlannedCommands[] = {
        // Networking. There is no driver datapath and no stack: virtio_net only
        // detects the device and never increments its counters. That is why
        // netinfo was reporting zeros rather than showing link state.
        { "netinfo", nullptr, Category::Devices, Availability::NeedsNetworking, Risk::ReadOnly, CapabilityNone,
            not_implemented, "show network state (no stack yet)", nullptr, nullptr },
        { "netconfig", nullptr, Category::Devices, Availability::NeedsNetworking, Risk::WritesState,
            CapabilityNetwork, not_implemented, "configure the network profile", nullptr, nullptr },
        { "hostname", nullptr, Category::Devices, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityAdmin, not_implemented, "set the device hostname", nullptr, nullptr },

        // Process management. Tasks exist but nothing can be killed or
        // supervised until processes do.
        { "kill", nullptr, Category::Scheduling, Availability::NeedsProcesses, Risk::Dangerous,
            CapabilityProcess, not_implemented, "stop a task or process", nullptr, nullptr },
        { "service", nullptr, Category::Scheduling, Availability::NeedsProcesses, Risk::WritesState,
            CapabilityProcess, not_implemented, "manage system services", nullptr, nullptr },

        // Identity commands are real and live in identity.cpp, backed by the
        // credential store.

        // Storage. Mounting works, but there is no persistent filesystem and no
        // writable block backend yet.
        { "umount", nullptr, Category::Storage, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityFileWrite, not_implemented, "remove a bind mount", nullptr, nullptr },
        { "du", nullptr, Category::Storage, Availability::NeedsPersistence, Risk::ReadOnly,
            CapabilityFileRead, not_implemented, "show per-user disk usage", nullptr, nullptr },
        { "df", nullptr, Category::Storage, Availability::NeedsPersistence, Risk::ReadOnly,
            CapabilityFileRead, not_implemented, "show filesystem capacity", nullptr, nullptr },

        // Provisioning. The host side of this pipeline is real (tools/provision
        // will grow from scripts/tinyos-image.sh); the target side needs a hash
        // implementation and persistent storage first.
        { "tappinstall", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityAdmin, not_implemented, "install a package", nullptr, nullptr },
        { "tappremove", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::Dangerous,
            CapabilityAdmin, not_implemented, "remove a package", nullptr, nullptr },
        { "tappverify", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::ReadOnly,
            CapabilityFileRead, not_implemented, "verify a package signature", nullptr, nullptr },
        { "package", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityAdmin, not_implemented, "manage installed packages", nullptr, nullptr },
        { "provision", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::Dangerous,
            CapabilityAdmin, not_implemented, "activate an image on a target", nullptr, nullptr },
        { "rollback", nullptr, Category::Provisioning, Availability::NeedsPersistence, Risk::Dangerous,
            CapabilityAdmin, not_implemented, "restore the previous image slot", nullptr, nullptr },
        { "deploy", nullptr, Category::Provisioning, Availability::NeedsNetworking, Risk::Dangerous,
            CapabilityAdmin, not_implemented, "send an image to a target", nullptr, nullptr },
        { "provisionui", nullptr, Category::Provisioning, Availability::NeedsProcesses, Risk::WritesState,
            CapabilityAdmin, not_implemented, "open the provisioning workbench", nullptr, nullptr },
        { "provisionvariant", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "manage device variants (host tool)", nullptr, nullptr },
        { "provisionresources", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "estimate resource budgets (host tool)", nullptr, nullptr },
        { "imagebuild", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "build an image (host tool)", nullptr, nullptr },
        { "imagesign", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "sign an image (host tool)", nullptr, nullptr },
        { "imageencrypt", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "encrypt an image (host tool)", nullptr, nullptr },
        { "keygen", nullptr, Category::Provisioning, Availability::HostOnly, Risk::ReadOnly,
            CapabilityNone, not_implemented, "create signing keys (host tool)", nullptr, nullptr },
        { "remoteaccess", nullptr, Category::Provisioning, Availability::NeedsNetworking, Risk::Dangerous,
            CapabilityAdmin, not_implemented, "manage remote access", nullptr, nullptr },

        // Power and console. `reboot` is real and lives with the diagnostics;
        // shutdown needs ACPI, and video mode selection needs a framebuffer.
        { "videomode", nullptr, Category::Ui, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityAdmin, not_implemented, "select console or framebuffer mode", nullptr, nullptr },
        { "terminaltheme", nullptr, Category::Ui, Availability::NeedsPersistence, Risk::WritesState,
            CapabilityNone, not_implemented, "select a terminal colour theme", nullptr, nullptr }
    };
}

namespace tinyos::shell::commands
{
    void register_planned_commands()
    {
        tinyos::libshell::register_table(PlannedCommands,
            sizeof(PlannedCommands) / sizeof(PlannedCommands[0]));
    }
}
