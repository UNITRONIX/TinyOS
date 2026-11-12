#pragma once

namespace tinyos::shell::commands
{
    // Command modules. Each one registers its table with the libshell registry
    // during shell initialisation. They deliberately live outside libshell
    // because they call kernel subsystems (VFS, scheduler, devices); libshell
    // itself stays free of kernel dependencies so it can be host-tested.
    void register_fs_commands();
    void register_shell_commands();
    void register_system_commands();
    void register_scheduler_commands();
    void register_identity_commands();

    // Declares commands that are not implemented yet, with the real blocker in
    // their Availability. Registering them means `netconfig` reports
    // "not available yet (needs-networking)" instead of "command not found".
    void register_planned_commands();

    // Registers every module. Called once during kernel initialisation.
    void register_all();
}
