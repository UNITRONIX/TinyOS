// Host unit tests for the command registry.
//
// The registry replaces four divergent command lists (kernel/admin/tools.cpp,
// the HelpCommand arrays in shell.cpp, shell/completion.cpp and the hard-coded
// tools.txt text). These tests pin the invariants that kept them in sync before.

#include <cstdio>
#include <cstring>

#include <tinyos/libshell/registry.hpp>

namespace
{
    int g_failures = 0;
    int g_checks = 0;

    void check(bool condition, const char* description)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::printf("  FAIL  %s\n", description);
        }
    }

    using tinyos::libshell::Availability;
    using tinyos::libshell::Category;
    using tinyos::libshell::Command;
    using tinyos::libshell::Context;
    using tinyos::libshell::Risk;
    using tinyos::libshell::Status;

    Status echo_handler(Context& context)
    {
        if (context.argc < 2)
        {
            return Status::Usage;
        }

        context.out.write(context.argv[1]);
        return Status::Ok;
    }

    const char* const EchoAliases[] = { "print", nullptr };

    const Command EchoCommand = {
        "echo",
        EchoAliases,
        Category::Shell,
        Availability::Real,
        Risk::ReadOnly,
        tinyos::libshell::CapabilityConsole,
        echo_handler,
        "print text",
        "echo <text>",
        "echo hello"
    };

    Status placeholder_handler(Context&)
    {
        return Status::Unavailable;
    }

    const Command PlannedCommand = {
        "netconfig",
        nullptr,
        Category::Devices,
        Availability::NeedsNetworking,
        Risk::WritesState,
        tinyos::libshell::CapabilityNetwork,
        placeholder_handler,
        "configure network profile",
        nullptr,
        nullptr
    };

    // File scope so registered pointers stay valid and comparable across tests.
    const Command CommandTable[] = { EchoCommand, PlannedCommand };

    void test_registration_and_lookup()
    {
        tinyos::libshell::reset_registry();
        tinyos::libshell::register_command(EchoCommand);

        check(tinyos::libshell::command_count() == 1, "one command registered");
        check(tinyos::libshell::find("echo") == &EchoCommand, "find by name");
        check(tinyos::libshell::find("print") == &EchoCommand, "find by alias");
        check(tinyos::libshell::find("missing") == nullptr, "unknown returns null");
        check(tinyos::libshell::find(nullptr) == nullptr, "null name returns null");

        // Re-registering the same name replaces instead of duplicating.
        tinyos::libshell::register_command(EchoCommand);
        check(tinyos::libshell::command_count() == 1, "re-registration is idempotent");

        // Null name or handler is ignored rather than corrupting the table.
        const Command broken = { nullptr, nullptr, Category::Shell, Availability::Real, Risk::ReadOnly,
            0, echo_handler, "broken", nullptr, nullptr };
        tinyos::libshell::register_command(broken);
        check(tinyos::libshell::command_count() == 1, "invalid command rejected");
    }

    void test_table_registration()
    {
        tinyos::libshell::reset_registry();
        // The table is file scope so the registered addresses stay stable; a
        // stack table would make address comparisons in tests meaningless.
        const Command* found = nullptr;

        tinyos::libshell::register_table(CommandTable, 2);
        check(tinyos::libshell::command_count() == 2, "table registered");

        found = tinyos::libshell::find("netconfig");
        check(found != nullptr && std::strcmp(found->name, "netconfig") == 0, "table lookup");
    }

    void test_table_duplicates_rejected()
    {
        const char* error = nullptr;

        check(tinyos::libshell::register_table_checked(CommandTable, 2, &error), "valid table accepted");

        // A duplicate inside one module's table is always a coding error.
        const Command duplicates[] = { EchoCommand, EchoCommand };
        check(!tinyos::libshell::register_table_checked(duplicates, 2, &error), "duplicate inside table rejected");
        check(error != nullptr && std::strcmp(error, "duplicate name inside table") == 0, "duplicate reported");

        // Alias collision inside a table is rejected too.
        const char* const aliases[] = { "echo", nullptr };
        const Command alias_collision = { "shout", aliases, Category::Shell, Availability::Real, Risk::ReadOnly,
            0, echo_handler, "shout", "shout <text>", nullptr };
        const Command with_collision[] = { EchoCommand, alias_collision };
        check(!tinyos::libshell::register_table_checked(with_collision, 2, &error),
            "alias collision inside table rejected");
    }

    void test_availability_is_honest()
    {
        check(tinyos::libshell::is_available(Availability::Real), "Real is available");
        check(!tinyos::libshell::is_available(Availability::HostOnly), "HostOnly not available in kernel");
        check(!tinyos::libshell::is_available(Availability::NeedsPersistence), "NeedsPersistence not available");
        check(!tinyos::libshell::is_available(Availability::NeedsProcesses), "NeedsProcesses not available");
        check(!tinyos::libshell::is_available(Availability::NeedsNetworking), "NeedsNetworking not available");

        // Every state must have a printable name, or `tools` would print null.
        for (int value = 0; value <= static_cast<int>(Availability::NeedsNetworking); ++value)
        {
            const char* name = tinyos::libshell::availability_name(static_cast<Availability>(value));
            check(name != nullptr && name[0] != '\0', "availability name present");
        }
    }

    void test_completion_reads_registry()
    {
        tinyos::libshell::reset_registry();
        tinyos::libshell::register_table(CommandTable, 2);

        const Command* results[8] = {};
        check(tinyos::libshell::complete("ec", results, 8) == 1, "prefix ec matches one");
        check(results[0] != nullptr && std::strcmp(results[0]->name, "echo") == 0, "prefix match is echo");

        check(tinyos::libshell::complete("ne", results, 8) == 1, "prefix ne matches netconfig");
        check(tinyos::libshell::complete("zzz", results, 8) == 0, "no match");
        check(tinyos::libshell::complete("", results, 2) == 2, "empty prefix limited by max_results");

        // A command that does not exist must never be suggested. The old
        // completion table offered gfxterm/desktop/desktopinfo long after the
        // desktop stack was deleted.
        check(tinyos::libshell::complete("gfx", results, 8) == 0, "removed desktop commands not suggested");
        check(tinyos::libshell::find("gfxterm") == nullptr, "gfxterm is gone");
        check(tinyos::libshell::find("desktop") == nullptr, "desktop is gone");
    }

    void test_validation_invariants()
    {
        const char* error = nullptr;

        tinyos::libshell::reset_registry();
        tinyos::libshell::register_command(EchoCommand);
        tinyos::libshell::register_command(PlannedCommand);
        check(tinyos::libshell::validation_self_test(&error), "valid table passes");

        // An available command must document its usage; otherwise `help <cmd>`
        // has nothing to show.
        const Command no_usage = { "broken", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly,
            0, echo_handler, "summary", nullptr, nullptr };
        tinyos::libshell::register_command(no_usage);
        check(!tinyos::libshell::validation_self_test(&error), "available command without usage rejected");
        check(error != nullptr && std::strcmp(error, "available command without usage text") == 0,
            "usage error reported");

        // A planned command may omit usage while it is unimplemented.
        tinyos::libshell::reset_registry();
        tinyos::libshell::register_command(PlannedCommand);
        check(tinyos::libshell::validation_self_test(&error), "planned command may omit usage");

        // register_command replaces rather than duplicating, so the table can
        // never contain two entries under one name. The guard against real
        // duplicates lives in register_table_checked().
        tinyos::libshell::reset_registry();
        tinyos::libshell::register_table(&EchoCommand, 1);
        const Command replacement = { "echo", nullptr, Category::Shell, Availability::Real, Risk::ReadOnly,
            0, echo_handler, "replacement", "echo", nullptr };
        tinyos::libshell::register_command(replacement);
        check(tinyos::libshell::command_count() == 1, "re-registration replaces, not duplicates");
        check(tinyos::libshell::validation_self_test(&error), "replaced table still valid");
    }

    void test_categories_complete()
    {
        check(tinyos::libshell::category_count() > 0, "categories exist");
        for (size_t index = 0; index < tinyos::libshell::category_count(); ++index)
        {
            const Category category = tinyos::libshell::category_at(index);
            const char* name = tinyos::libshell::category_name(category);
            check(name != nullptr && name[0] != '\0', "category name present");
        }
    }

    void test_risk_names()
    {
        check(std::strcmp(tinyos::libshell::risk_name(Risk::ReadOnly), "read-only") == 0, "risk read-only");
        check(std::strcmp(tinyos::libshell::risk_name(Risk::WritesState), "writes-state") == 0, "risk writes");
        check(std::strcmp(tinyos::libshell::risk_name(Risk::Dangerous), "dangerous") == 0, "risk dangerous");
    }
}

int main()
{
    test_registration_and_lookup();
    test_table_registration();
    test_table_duplicates_rejected();
    test_availability_is_honest();
    test_completion_reads_registry();
    test_validation_invariants();
    test_categories_complete();
    test_risk_names();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
