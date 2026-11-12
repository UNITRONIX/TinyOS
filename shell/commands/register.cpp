// Aggregated command registration.
//
// Each module owns its own table and registers it here, so adding a command
// group never touches the shell core.

#include <tinyos/kernel/klog.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/shell/commands/commands.hpp>

namespace tinyos::shell::commands
{
    void register_all()
    {
        register_fs_commands();
        register_shell_commands();
        register_system_commands();
        register_scheduler_commands();
        register_identity_commands();
        register_planned_commands();

        // The registry's own invariants are checked at boot. A duplicate name or
        // a command missing its usage text is a build-time mistake that would
        // otherwise surface as a confusing runtime failure.
        const char* error = nullptr;
        if (!tinyos::libshell::validation_self_test(&error))
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error,
                "Command registry validation failed.");
            if (error != nullptr)
            {
                tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, error);
            }
            return;
        }

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info,
            "Command registry loaded and validated.");
    }
}
