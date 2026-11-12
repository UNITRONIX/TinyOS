// Compatibility projection of the command registry.
//
// See the header for why this file no longer owns a table. The important
// property here is that there is exactly one list of commands in the binary:
// libshell::registry. Everything below reads from it.

#include <tinyos/kernel/admin/tools.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/libshell/registry.hpp>

namespace
{
    bool g_ready = false;

    // Scratch slot for at(). A caller that holds the pointer across another
    // registry query sees the newer entry, which is documented in the header.
    tinyos::kernel::admin::tools::Tool g_scratch = {};

    tinyos::kernel::admin::tools::Category map_category(tinyos::libshell::Category category)
    {
        using Source = tinyos::libshell::Category;
        using Target = tinyos::kernel::admin::tools::Category;

        switch (category)
        {
        case Source::Shell:
            return Target::Shell;
        case Source::Files:
            return Target::Files;
        case Source::Storage:
            return Target::Storage;
        case Source::Devices:
            return Target::Devices;
        case Source::Memory:
            return Target::Memory;
        case Source::Runtime:
            return Target::Runtime;
        case Source::Security:
            return Target::Security;
        case Source::Ui:
            return Target::Ui;
        case Source::Scheduling:
            return Target::Scheduling;
        case Source::Power:
            return Target::Power;
        case Source::Provisioning:
        case Source::Development:
            return Target::Development;
        }

        return Target::Shell;
    }

    tinyos::kernel::admin::tools::State map_state(tinyos::libshell::Availability availability)
    {
        // Only a command that can run right now is Ready. Everything else is
        // Planned regardless of which blocker it has, which is the honest
        // collapse of a five-state availability onto the historical pair.
        return tinyos::libshell::is_available(availability)
            ? tinyos::kernel::admin::tools::State::Ready
            : tinyos::kernel::admin::tools::State::Planned;
    }

    void fill(tinyos::kernel::admin::tools::Tool& tool, const tinyos::libshell::Command& command)
    {
        tool.command = command.name;
        tool.purpose = command.summary;
        tool.category = map_category(command.category);
        tool.state = map_state(command.availability);
        tool.writes_state = command.risk != tinyos::libshell::Risk::ReadOnly;
        tool.high_risk = command.risk == tinyos::libshell::Risk::Dangerous;
    }
}

namespace tinyos::kernel::admin::tools
{
    void initialize()
    {
        // The registry is populated by the shell's command modules, which run
        // before this. If it is still empty the manifest has nothing truthful to
        // report, so say that instead of counting zero silently.
        const size_t registered = tinyos::libshell::command_count();
        g_ready = registered > 0;

        if (g_ready)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info,
                "Tool manifest projected from the command registry.");
            return;
        }

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn,
            "Tool manifest has no commands: the registry is empty.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t count()
    {
        return tinyos::libshell::command_count();
    }

    size_t ready_count()
    {
        size_t ready = 0;
        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command != nullptr && tinyos::libshell::is_available(command->availability))
            {
                ++ready;
            }
        }

        return ready;
    }

    size_t planned_count()
    {
        return count() - ready_count();
    }

    size_t write_tool_count()
    {
        size_t writes = 0;
        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command != nullptr && command->risk != tinyos::libshell::Risk::ReadOnly)
            {
                ++writes;
            }
        }

        return writes;
    }

    size_t high_risk_count()
    {
        size_t dangerous = 0;
        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command != nullptr && command->risk == tinyos::libshell::Risk::Dangerous)
            {
                ++dangerous;
            }
        }

        return dangerous;
    }

    const Tool* at(size_t index)
    {
        const auto* command = tinyos::libshell::command_at(index);
        if (command == nullptr)
        {
            return nullptr;
        }

        fill(g_scratch, *command);
        return &g_scratch;
    }

    const Tool* find(const char* command_name)
    {
        const auto* command = tinyos::libshell::find(command_name);
        if (command == nullptr)
        {
            return nullptr;
        }

        fill(g_scratch, *command);
        return &g_scratch;
    }

    bool validation_self_test()
    {
        if (!g_ready || count() == 0)
        {
            return false;
        }

        // The projection must agree with the registry it claims to describe.
        // These are the invariants that the old hand-maintained table violated.
        const size_t total = count();
        const size_t ready = ready_count();

        if (ready > total)
        {
            return false;
        }

        // Every command the projection reports as Ready must resolve through the
        // registry and must be runnable. This is the check that would have caught
        // `useradd` claiming Ready while printing "not wired to a create path".
        for (size_t index = 0; index < total; ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command == nullptr)
            {
                return false;
            }

            const auto* tool = find(command->name);
            if (tool == nullptr)
            {
                return false;
            }

            if (tool->state == State::Ready && !tinyos::libshell::is_available(command->availability))
            {
                return false;
            }
        }

        // A representative sample must exist, because the shell registers these
        // unconditionally. If one is missing the registration order broke.
        return find("files") != nullptr &&
            find("help") != nullptr &&
            find("tools") != nullptr &&
            find("riskinfo") != nullptr &&
            find("ps") != nullptr &&
            find("meminfo") != nullptr &&
            find("cd") != nullptr &&
            find("whoami") != nullptr;
    }

    const char* category_name(Category category)
    {
        switch (category)
        {
        case Category::Shell:
            return "shell";
        case Category::Files:
            return "files";
        case Category::Storage:
            return "storage";
        case Category::Devices:
            return "devices";
        case Category::Memory:
            return "memory";
        case Category::Runtime:
            return "runtime";
        case Category::Security:
            return "security";
        case Category::Ui:
            return "ui";
        case Category::Scheduling:
            return "scheduling";
        case Category::Power:
            return "power";
        case Category::Development:
            return "development";
        }

        return "unknown";
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Ready:
            return "ready";
        case State::Planned:
            return "planned";
        }

        return "unknown";
    }
}
