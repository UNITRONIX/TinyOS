// Shell-introspection commands.
//
// help, tools, tool, toolinfo and riskinfo all read the command registry, so
// there is exactly one command list in the binary. Previously four copies
// existed and drifted: kernel/admin/tools.cpp said 122 commands were Ready,
// HelpCommand listed 119, completion offered 32 (including deleted desktop
// commands) and /system/tools.txt was hand-written text (defects D21, D22).

#include <tinyos/kernel/klog.hpp>
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

    // Capability is a plain enum, so its enumerators are namespace-scoped.
    using tinyos::libshell::CapabilityNone;

    void write_unsigned(Stream& stream, size_t value)
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

    // Pads `text` to `width` so columns line up in the VGA text console.
    void write_padded(Stream& stream, const char* text, size_t width)
    {
        const size_t length = tinyos::libk::text::length(text);
        stream.write(text);
        for (size_t index = length; index < width; ++index)
        {
            stream.put_char(' ');
        }
    }

    void print_command_line(Context& context, const tinyos::libshell::Command& command, bool verbose)
    {
        write_padded(context.out, command.name, 14);
        context.out.write(command.summary != nullptr ? command.summary : "(no summary)");

        if (!verbose)
        {
            context.out.put_char('\n');
            return;
        }

        context.out.write("  [");
        context.out.write(tinyos::libshell::availability_name(command.availability));
        context.out.write(", ");
        context.out.write(tinyos::libshell::risk_name(command.risk));
        context.out.put_char(']');
        context.out.put_char('\n');

        if (command.usage != nullptr)
        {
            context.out.write("               usage: ");
            context.out.line(command.usage);
        }
    }

    Status cmd_help(Context& context)
    {
        // `help <command>` shows one entry in detail.
        if (context.argc > 1)
        {
            const auto* command = tinyos::libshell::find(context.argv[1]);
            if (command == nullptr)
            {
                context.err.write(context.argv[1]);
                context.err.line(": no such command");
                return Status::NotFound;
            }

            context.out.write(command->name);
            context.out.put_char('\n');
            context.out.write("  summary     : ");
            context.out.line(command->summary != nullptr ? command->summary : "(none)");
            context.out.write("  category    : ");
            context.out.line(tinyos::libshell::category_name(command->category));
            context.out.write("  availability: ");
            context.out.line(tinyos::libshell::availability_name(command->availability));
            context.out.write("  risk        : ");
            context.out.line(tinyos::libshell::risk_name(command->risk));

            if (command->usage != nullptr)
            {
                context.out.write("  usage       : ");
                context.out.line(command->usage);
            }

            if (command->example != nullptr)
            {
                context.out.write("  example     : ");
                context.out.line(command->example);
            }

            return Status::Ok;
        }

        // Listing groups by category, in the registry's own category order, so
        // the output order is stable and matches tools.
        for (size_t index = 0; index < tinyos::libshell::category_count(); ++index)
        {
            const Category category = tinyos::libshell::category_at(index);

            // Only print a heading for a category that has commands.
            size_t matches = 0;
            for (size_t command_index = 0; command_index < tinyos::libshell::command_count(); ++command_index)
            {
                const auto* command = tinyos::libshell::command_at(command_index);
                if (command != nullptr && command->category == category)
                {
                    ++matches;
                }
            }

            if (matches == 0)
            {
                continue;
            }

            context.out.line(tinyos::libshell::category_name(category));
            for (size_t command_index = 0; command_index < tinyos::libshell::command_count(); ++command_index)
            {
                const auto* command = tinyos::libshell::command_at(command_index);
                if (command != nullptr && command->category == category)
                {
                    print_command_line(context, *command, false);
                }
            }
        }

        context.out.line("");
        context.out.line("help <command> shows details. tools lists availability and risk.");
        return Status::Ok;
    }

    Status cmd_tools(Context& context)
    {
        size_t available = 0;
        size_t pending = 0;

        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command == nullptr)
            {
                continue;
            }

            print_command_line(context, *command, true);
            if (tinyos::libshell::is_available(command->availability))
            {
                ++available;
            }
            else
            {
                ++pending;
            }
        }

        context.out.put_char('\n');
        context.out.write("Registered : ");
        write_unsigned(context.out, tinyos::libshell::command_count());
        context.out.put_char('\n');
        context.out.write("Available  : ");
        write_unsigned(context.out, available);
        context.out.put_char('\n');
        context.out.write("Not yet    : ");
        write_unsigned(context.out, pending);
        context.out.put_char('\n');

        return Status::Ok;
    }

    Status cmd_toolinfo(Context& context)
    {
        size_t by_availability[5] = {};

        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command == nullptr)
            {
                continue;
            }

            const size_t slot = static_cast<size_t>(command->availability);
            if (slot < 5)
            {
                ++by_availability[slot];
            }
        }

        context.out.write("Total        : ");
        write_unsigned(context.out, tinyos::libshell::command_count());
        context.out.put_char('\n');

        for (size_t slot = 0; slot < 5; ++slot)
        {
            const auto availability = static_cast<Availability>(slot);
            write_padded(context.out, tinyos::libshell::availability_name(availability), 18);
            write_unsigned(context.out, by_availability[slot]);
            context.out.put_char('\n');
        }

        return Status::Ok;
    }

    Status cmd_tool(Context& context)
    {
        if (context.argc < 2)
        {
            context.err.line("usage: tool <command>");
            return Status::Usage;
        }

        const auto* command = tinyos::libshell::find(context.argv[1]);
        if (command == nullptr)
        {
            context.err.write(context.argv[1]);
            context.err.line(": no such command");
            return Status::NotFound;
        }

        print_command_line(context, *command, true);
        return Status::Ok;
    }

    Status cmd_riskinfo(Context& context)
    {
        // Lists only commands that can change state, which is the set a user
        // should think about before running.
        size_t shown = 0;
        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command == nullptr || command->risk == Risk::ReadOnly)
            {
                continue;
            }

            print_command_line(context, *command, true);
            ++shown;
        }

        context.out.put_char('\n');
        context.out.write("State-changing commands: ");
        write_unsigned(context.out, shown);
        context.out.put_char('\n');

        return Status::Ok;
    }

    Status cmd_aliases(Context& context)
    {
        size_t shown = 0;
        for (size_t index = 0; index < tinyos::libshell::command_count(); ++index)
        {
            const auto* command = tinyos::libshell::command_at(index);
            if (command == nullptr || command->aliases == nullptr)
            {
                continue;
            }

            for (size_t alias = 0; command->aliases[alias] != nullptr; ++alias)
            {
                write_padded(context.out, command->aliases[alias], 14);
                context.out.write("-> ");
                context.out.line(command->name);
                ++shown;
            }
        }

        if (shown == 0)
        {
            context.out.line("No aliases registered.");
        }

        return Status::Ok;
    }

    Status cmd_clear(Context& context)
    {
        // Clearing writes a form-feed-free sequence: the console driver has no
        // dedicated clear hook exposed to commands, so this prints enough
        // newlines to scroll the screen. It is honest about being a scroll, not
        // a memory clear.
        for (size_t index = 0; index < 25; ++index)
        {
            context.out.put_char('\n');
        }

        return Status::Ok;
    }

    Status cmd_echo(Context& context)
    {
        for (int index = 1; index < context.argc; ++index)
        {
            if (index > 1)
            {
                context.out.put_char(' ');
            }

            context.out.write(context.argv[index]);
        }

        context.out.put_char('\n');
        return Status::Ok;
    }

    Status cmd_true(Context&)
    {
        return Status::Ok;
    }

    Status cmd_false(Context&)
    {
        return Status::Failed;
    }

    const char* const HelpAliases[] = { "?", nullptr };

    const tinyos::libshell::Command ShellCommands[] = {
        { "help", HelpAliases, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_help, "show commands or one command's details", "help [command]", "help chmod" },
        { "tools", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_tools, "list every command with availability and risk", "tools", "tools" },
        { "toolinfo", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_toolinfo, "summarise commands by availability", "toolinfo", "toolinfo" },
        { "tool", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_tool, "show one command in detail", "tool <command>", "tool mkdir" },
        { "riskinfo", nullptr, Category::Security, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_riskinfo, "list state-changing commands", "riskinfo", "riskinfo" },
        { "aliases", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_aliases, "list compatibility aliases", "aliases", "aliases" },
        { "clear", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_clear, "scroll the screen", "clear", "clear" },
        { "echo", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_echo, "print its arguments", "echo <text>", "echo hello" },
        { "true", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_true, "succeed without doing anything", "true", "true" },
        { "false", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_false, "fail without doing anything", "false", "false" }
    };
}

namespace tinyos::shell::commands
{
    void register_shell_commands()
    {
        tinyos::libshell::register_table(ShellCommands,
            sizeof(ShellCommands) / sizeof(ShellCommands[0]));
    }
}
