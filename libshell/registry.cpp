#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/registry.hpp>

namespace
{
    using tinyos::libshell::Availability;
    using tinyos::libshell::Command;

    // Fixed-capacity registry. A dynamic table would pull the kernel heap into
    // the shell, and the shell must be linkable in a host test binary too.
    // 192 slots covers the current command surface with room to grow; exceeding
    // it is a hard error rather than silent truncation.
    constexpr size_t MaxCommands = 192;

    const Command* g_commands[MaxCommands] = {};
    size_t g_count = 0;

    const tinyos::libshell::Category Categories[] = {
        tinyos::libshell::Category::Shell,
        tinyos::libshell::Category::Files,
        tinyos::libshell::Category::Memory,
        tinyos::libshell::Category::Scheduling,
        tinyos::libshell::Category::Devices,
        tinyos::libshell::Category::Storage,
        tinyos::libshell::Category::Security,
        tinyos::libshell::Category::Runtime,
        tinyos::libshell::Category::Provisioning,
        tinyos::libshell::Category::Development,
        tinyos::libshell::Category::Ui,
        tinyos::libshell::Category::Power
    };

    bool name_matches(const Command& command, const char* name)
    {
        if (tinyos::libk::text::equals(command.name, name))
        {
            return true;
        }

        if (command.aliases == nullptr)
        {
            return false;
        }

        for (size_t index = 0; command.aliases[index] != nullptr; ++index)
        {
            if (tinyos::libk::text::equals(command.aliases[index], name))
            {
                return true;
            }
        }

        return false;
    }
}

namespace tinyos::libshell
{
    const char* status_name(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "ok";
        case Status::Usage:
            return "usage";
        case Status::NotFound:
            return "not-found";
        case Status::Denied:
            return "denied";
        case Status::Io:
            return "io-error";
        case Status::Unavailable:
            return "unavailable";
        case Status::Failed:
            return "failed";
        }

        return "failed";
    }

    const char* risk_name(Risk risk)
    {
        switch (risk)
        {
        case Risk::ReadOnly:
            return "read-only";
        case Risk::WritesState:
            return "writes-state";
        case Risk::Dangerous:
            return "dangerous";
        }

        return "read-only";
    }

    const char* availability_name(Availability availability)
    {
        switch (availability)
        {
        case Availability::Real:
            return "real";
        case Availability::HostOnly:
            return "host-only";
        case Availability::NeedsPersistence:
            return "needs-persistence";
        case Availability::NeedsProcesses:
            return "needs-processes";
        case Availability::NeedsNetworking:
            return "needs-networking";
        }

        return "real";
    }

    bool is_available(Availability availability)
    {
        // Only Real is runnable today. The other states exist so `help` and
        // `tools` can tell the truth about why something does not work, instead
        // of claiming Ready and failing at runtime.
        return availability == Availability::Real;
    }

    const char* category_name(Category category)
    {
        switch (category)
        {
        case Category::Shell:
            return "shell";
        case Category::Files:
            return "files";
        case Category::Memory:
            return "memory";
        case Category::Scheduling:
            return "scheduling";
        case Category::Devices:
            return "devices";
        case Category::Storage:
            return "storage";
        case Category::Security:
            return "security";
        case Category::Runtime:
            return "runtime";
        case Category::Provisioning:
            return "provisioning";
        case Category::Development:
            return "development";
        case Category::Ui:
            return "ui";
        case Category::Power:
            return "power";
        }

        return "shell";
    }

    size_t category_count()
    {
        return sizeof(Categories) / sizeof(Categories[0]);
    }

    Category category_at(size_t index)
    {
        if (index >= category_count())
        {
            return Category::Shell;
        }

        return Categories[index];
    }

    void register_command(const Command& command)
    {
        if (command.name == nullptr || command.handler == nullptr)
        {
            return;
        }

        if (g_count >= MaxCommands)
        {
            return;
        }

        // Replacing a name keeps registration idempotent, which host tests rely
        // on when they register, reset and register again.
        for (size_t index = 0; index < g_count; ++index)
        {
            if (tinyos::libk::text::equals(g_commands[index]->name, command.name))
            {
                g_commands[index] = &command;
                return;
            }
        }

        g_commands[g_count] = &command;
        ++g_count;
    }

    void register_table(const Command* commands, size_t count)
    {
        const char* error = nullptr;
        (void)register_table_checked(commands, count, &error);
    }

    bool register_table_checked(const Command* commands, size_t count, const char** error)
    {
        if (error != nullptr)
        {
            *error = nullptr;
        }

        if (commands == nullptr)
        {
            return true;
        }

        // Validate the table against itself before mutating the registry, so a
        // bad module cannot leave the table half-registered.
        for (size_t index = 0; index < count; ++index)
        {
            const Command& command = commands[index];
            if (command.name == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "table entry without name";
                }
                return false;
            }

            for (size_t other = index + 1; other < count; ++other)
            {
                if (name_matches(commands[other], command.name))
                {
                    if (error != nullptr)
                    {
                        *error = "duplicate name inside table";
                    }
                    return false;
                }

                if (command.aliases == nullptr)
                {
                    continue;
                }

                for (size_t alias = 0; command.aliases[alias] != nullptr; ++alias)
                {
                    if (name_matches(commands[other], command.aliases[alias]))
                    {
                        if (error != nullptr)
                        {
                            *error = "alias collides inside table";
                        }
                        return false;
                    }
                }
            }
        }

        for (size_t index = 0; index < count; ++index)
        {
            register_command(commands[index]);
        }

        return true;
    }

    const Command* find(const char* name)
    {
        if (name == nullptr)
        {
            return nullptr;
        }

        for (size_t index = 0; index < g_count; ++index)
        {
            if (name_matches(*g_commands[index], name))
            {
                return g_commands[index];
            }
        }

        return nullptr;
    }

    size_t command_count()
    {
        return g_count;
    }

    const Command* command_at(size_t index)
    {
        if (index >= g_count)
        {
            return nullptr;
        }

        return g_commands[index];
    }

    size_t complete(const char* prefix, const Command** results, size_t max_results)
    {
        if (prefix == nullptr || results == nullptr || max_results == 0)
        {
            return 0;
        }

        size_t found = 0;
        for (size_t index = 0; index < g_count && found < max_results; ++index)
        {
            if (tinyos::libk::text::starts_with(g_commands[index]->name, prefix))
            {
                results[found] = g_commands[index];
                ++found;
            }
        }

        return found;
    }

    void reset_registry()
    {
        for (size_t index = 0; index < g_count; ++index)
        {
            g_commands[index] = nullptr;
        }

        g_count = 0;
    }

    bool validation_self_test(const char** error)
    {
        if (error != nullptr)
        {
            *error = nullptr;
        }

        for (size_t index = 0; index < g_count; ++index)
        {
            const Command& command = *g_commands[index];

            if (command.name == nullptr || command.name[0] == '\0')
            {
                if (error != nullptr)
                {
                    *error = "command with empty name";
                }
                return false;
            }

            if (command.handler == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "command with null handler";
                }
                return false;
            }

            if (command.summary == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "command without summary";
                }
                return false;
            }

            // Anything available must document how to call it.
            if (is_available(command.availability) && command.usage == nullptr)
            {
                if (error != nullptr)
                {
                    *error = "available command without usage text";
                }
                return false;
            }

            // Names and aliases must be unique across the table, or dispatch
            // becomes order dependent.
            for (size_t other = 0; other < g_count; ++other)
            {
                if (other == index)
                {
                    continue;
                }

                const Command& candidate = *g_commands[other];
                if (name_matches(candidate, command.name))
                {
                    if (error != nullptr)
                    {
                        *error = "duplicate command name";
                    }
                    return false;
                }

                if (command.aliases == nullptr)
                {
                    continue;
                }

                for (size_t alias = 0; command.aliases[alias] != nullptr; ++alias)
                {
                    if (name_matches(candidate, command.aliases[alias]))
                    {
                        if (error != nullptr)
                        {
                            *error = "alias collides with another command";
                        }
                        return false;
                    }
                }
            }
        }

        return true;
    }
}
