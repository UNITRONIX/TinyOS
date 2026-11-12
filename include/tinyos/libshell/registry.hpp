#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/libshell/session.hpp>
#include <tinyos/libshell/stream.hpp>

namespace tinyos::libshell
{
    // Exit status codes. Commands return one of these; the execution engine
    // propagates them through `&&` and `||` and the shell prints them nowhere by
    // default, exactly like a Unix shell.
    enum class Status : int
    {
        Ok = 0,
        Usage = 1,
        NotFound = 2,
        Denied = 3,
        Io = 4,
        Unavailable = 5,
        Failed = 6
    };

    const char* status_name(Status status);

    // How much a command can actually change the system. Reported by `riskinfo`
    // so a destructive command is identifiable before it runs.
    enum class Risk : uint8_t
    {
        ReadOnly,
        WritesState,
        Dangerous
    };

    const char* risk_name(Risk risk);

    // What a command needs before it can do its job.
    //
    // This replaces the previous binary Ready/Planned flag in
    // kernel/admin/tools.cpp, which labelled roughly forty unimplemented
    // commands as Ready and left the shell to print "not wired to a create path
    // yet" at runtime (defect D21).
    enum class Availability : uint8_t
    {
        // Implemented and usable right now.
        Real,

        // Works, but only in the development/host workflow.
        HostOnly,

        // Needs persistent storage, which is RAMFS-only until M5.
        NeedsPersistence,

        // Needs user processes (ring 3), so not before M6.
        NeedsProcesses,

        // Needs a network stack, which does not exist yet.
        NeedsNetworking
    };

    const char* availability_name(Availability availability);

    // True when the command can run in the current build.
    bool is_available(Availability availability);

    // Command category, used to group `help` output.
    enum class Category : uint8_t
    {
        Shell,
        Files,
        Memory,
        Scheduling,
        Devices,
        Storage,
        Security,
        Runtime,
        Provisioning,
        Development,
        Ui,
        Power
    };

    const char* category_name(Category category);
    size_t category_count();
    Category category_at(size_t index);

    // Capability bits required to run a command. Matched against the session's
    // capabilities once sessions exist (M2c).
    enum Capability : uint32_t
    {
        CapabilityNone = 0,
        CapabilityConsole = 1u << 0,
        CapabilityFileRead = 1u << 1,
        CapabilityFileWrite = 1u << 2,
        CapabilityProcess = 1u << 3,
        CapabilityDevice = 1u << 4,
        CapabilityAdmin = 1u << 5,
        CapabilityNetwork = 1u << 6
    };

    // Everything a command may touch. A command never reaches for global state:
    // its working directory, identity and output all arrive here, which is what
    // lets two sessions run different commands simultaneously (defect D5).
    struct Context
    {
        // Argument vector. argv[0] is the command name as typed.
        const char* const* argv;
        int argc;

        Stream out;
        Stream err;

        // The caller's session. Non-null for any command dispatched by the
        // engine. Commands that change session state (`cd`, `set`, `unset`)
        // write through this pointer; everything else only reads it.
        Session* session;

        // Set when the command's output is being captured (pipeline or
        // redirection). Commands that open interactive full-screen UI must
        // refuse rather than emit escape sequences into a buffer.
        bool interactive;

        // Convenience: argv[arg] or nullptr. Bounds-checked.
        const char* arg(int index) const
        {
            if (index < 0 || index >= argc || argv == nullptr)
            {
                return nullptr;
            }

            return argv[index];
        }

        // Working directory, or "/" when no session is attached. Returned as a
        // value rather than a reference so a command can pass it to
        // path::resolve() without worrying about the session mutating mid-call.
        const char* cwd() const
        {
            if (session == nullptr || session->cwd[0] != '/')
            {
                return "/";
            }

            return session->cwd;
        }

        uint32_t uid() const { return session != nullptr ? session->uid : 0; }
        uint32_t gid() const { return session != nullptr ? session->gid : 0; }
        uint32_t capabilities() const { return session != nullptr ? session->capabilities : 0; }
    };

    using Handler = Status (*)(Context& context);

    struct Command
    {
        const char* name;

        // nullptr-terminated list of alternative names, or nullptr.
        const char* const* aliases;

        Category category;
        Availability availability;
        Risk risk;
        uint32_t required_capabilities;

        Handler handler;

        const char* summary;
        const char* usage;
        const char* example;
    };

    // Registration. Each command module publishes a table and calls
    // register_table() from its own initializer, or the shell links a single
    // list. The registry does not own the strings.
    void register_command(const Command& command);
    void register_table(const Command* commands, size_t count);

    // Same, but reports whether the table contained a duplicate name or alias
    // inside itself. A table is a single module's export, so a duplicate within
    // it is always a coding error rather than a deliberate override.
    bool register_table_checked(const Command* commands, size_t count, const char** error);

    // Lookup by name or alias. Returns nullptr when unknown.
    const Command* find(const char* name);

    // Iteration in registration order.
    size_t command_count();
    const Command* command_at(size_t index);

    // Prefix match for tab completion. Writes at most `max_results` matches into
    // `results` and returns how many were found. Because completion now reads
    // the same table as dispatch, it can no longer offer commands that do not
    // exist (the old completion table advertised the removed desktop commands
    // gfxterm/desktop/desktopinfo).
    size_t complete(const char* prefix, const Command** results, size_t max_results);

    // Drops all registrations. Used by host tests.
    void reset_registry();

    // Structural invariants that must hold for any command table: unique names
    // and aliases, non-null handlers for available commands, usage text present
    // for anything that takes arguments. Returns false and reports the first
    // violation through `error`.
    bool validation_self_test(const char** error);
}
