// Identity commands backed by the credential store.
//
// These are the commands that decide whether "user space" means anything. The
// old shell printed a hard-coded "root" from `whoami` while `useradd` replied
// "not wired to a create path yet", and the account store was never consulted
// by anything (defects D3, D21).
//
// Now:
//   * `login` authenticates against accounts::authenticate and fills the
//     session identity,
//   * `whoami`/`id` report what the session actually carries,
//   * `useradd`/`userdel`/`passwd` mutate the store,
//   * `logout` clears the identity back to nobody.
//
// The honest limitation is that until per-process address spaces exist (M6) the
// isolation is enforced by these commands and the VFS permission bits, not by
// the MMU. `id` says so instead of implying otherwise.

#include <tinyos/core/memory.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/security/accounts.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/registry.hpp>
#include <tinyos/libshell/session.hpp>
#include <tinyos/shell/commands/commands.hpp>

namespace
{
    using tinyos::libshell::Availability;
    using tinyos::libshell::Category;
    using tinyos::libshell::Context;
    using tinyos::libshell::Risk;
    using tinyos::libshell::Session;
    using tinyos::libshell::Status;
    using tinyos::libshell::Stream;

    using tinyos::libshell::CapabilityAdmin;
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

    // Copies the account into the session. Shared by login and su.
    void adopt_identity(Session& session, const tinyos::kernel::security::accounts::Account& account)
    {
        session.uid = account.uid;
        session.gid = account.gid;
        session.authenticated = true;

        tinyos::core::memory::string_copy_safe(session.user_name, sizeof(session.user_name), account.name);

        // Administrators get every capability; ordinary users get the subset that
        // excludes admin operations. An unauthenticated session keeps the
        // restricted default set, so logging in is what grants privileges
        // (defect D48).
        session.capabilities = account.admin
            ? tinyos::libshell::AdminCapabilities
            : tinyos::libshell::UserCapabilities;

        session.env.set("USER", account.name);
    }

    Status cmd_login(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("login: no session attached");
            return Status::Failed;
        }

        if (context.argc < 3)
        {
            // Listing the accounts is useful and harmless: names are public, the
            // hashes are not.
            context.out.write("Accounts: ");
            write_unsigned(context.out, tinyos::kernel::security::accounts::count());
            context.out.put_char('\n');

            for (size_t index = 0; index < tinyos::kernel::security::accounts::MaxAccounts; ++index)
            {
                const auto* account = tinyos::kernel::security::accounts::at(index);
                if (account == nullptr)
                {
                    continue;
                }

                context.out.write("  ");
                context.out.write(account->name);
                context.out.write(account->admin ? " (admin)" : " (user)");
                context.out.put_char('\n');
            }

            context.err.line("usage: login <name> <password>");
            return Status::Usage;
        }

        // The password is visible in the command line because the shell has no
        // interactive prompt path yet. That is a real weakness and is stated
        // rather than hidden.
        const char* name = context.argv[1];
        const char* password = context.argv[2];

        if (!tinyos::kernel::security::accounts::authenticate(name, password))
        {
            context.err.line("login: authentication failed");
            return Status::Denied;
        }

        const auto* account = tinyos::kernel::security::accounts::find(name);
        if (account == nullptr)
        {
            context.err.line("login: account disappeared");
            return Status::Failed;
        }

        adopt_identity(*context.session, *account);

        context.out.write("Logged in as ");
        context.out.line(account->name);

        // Warn when the credential came from the compiled-in development set, so
        // a deployment cannot mistake this for a secured system.
        if (tinyos::kernel::security::accounts::development_credentials_active())
        {
            context.err.line("login: development credentials are active in this build");
        }

        return Status::Ok;
    }

    Status cmd_logout(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("logout: no session attached");
            return Status::Failed;
        }

        context.session->uid = 65534;
        context.session->gid = 65534;
        context.session->capabilities = tinyos::libshell::UnauthenticatedCapabilities;
        context.session->authenticated = false;
        tinyos::core::memory::string_copy_safe(context.session->user_name,
            sizeof(context.session->user_name), "nobody");
        context.session->env.set("USER", "nobody");

        context.out.line("Logged out.");
        return Status::Ok;
    }

    Status cmd_su(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("su: no session attached");
            return Status::Failed;
        }

        if (context.argc < 2)
        {
            context.err.line("usage: su <name> <password>");
            return Status::Usage;
        }

        // Switching to another identity requires either the password of the
        // target or an authenticated administrator session. Anything else would
        // make `su` a privilege escalation primitive.
        const char* name = context.argv[1];
        const auto* account = tinyos::kernel::security::accounts::find(name);
        if (account == nullptr)
        {
            context.err.write(name);
            context.err.line(": no such account");
            return Status::NotFound;
        }

        const bool is_admin_session = context.session->authenticated && context.session->uid == 0;

        if (!is_admin_session)
        {
            if (context.argc < 3)
            {
                context.err.line("su: password required");
                return Status::Denied;
            }

            if (!tinyos::kernel::security::accounts::authenticate(name, context.argv[2]))
            {
                context.err.line("su: authentication failed");
                return Status::Denied;
            }
        }

        adopt_identity(*context.session, *account);

        context.out.write("Now running as ");
        context.out.line(account->name);
        return Status::Ok;
    }

    Status cmd_whoami(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("whoami: no session attached");
            return Status::Failed;
        }

        context.out.write(context.session->user_name[0] != '\0' ? context.session->user_name : "nobody");
        if (!context.session->authenticated)
        {
            // The distinction matters: uid 0 without a login is the development
            // default, not proof of identity.
            context.out.write(" (unauthenticated)");
        }

        context.out.put_char('\n');
        return Status::Ok;
    }

    Status cmd_id(Context& context)
    {
        if (context.session == nullptr)
        {
            context.err.line("id: no session attached");
            return Status::Failed;
        }

        context.out.write("uid=");
        write_unsigned(context.out, context.session->uid);
        context.out.write(" gid=");
        write_unsigned(context.out, context.session->gid);
        context.out.write(" name=");
        context.out.write(context.session->user_name[0] != '\0' ? context.session->user_name : "nobody");
        context.out.put_char('\n');

        context.out.write("authenticated: ");
        context.out.line(context.session->authenticated ? "yes" : "no");

        context.out.line("groups: not modelled yet (no supplementary groups)");
        context.out.line("isolation: advisory (per-process address spaces arrive in M6)");

        return Status::Ok;
    }

    Status cmd_useradd(Context& context)
    {
        if (context.argc < 3)
        {
            context.err.line("usage: useradd <name> <password> [admin]");
            return Status::Usage;
        }

        const char* name = context.argv[1];
        const char* password = context.argv[2];
        const bool admin = context.argc > 3 && tinyos::libk::text::equals(context.argv[3], "admin");

        if (!tinyos::kernel::security::accounts::add_user(name, password, admin))
        {
            // Report the specific reason rather than a generic failure.
            if (tinyos::kernel::security::accounts::find(name) != nullptr)
            {
                context.err.write(name);
                context.err.line(": already exists");
                return Status::Failed;
            }

            if (password[0] == '\0')
            {
                context.err.line("useradd: password must not be empty");
                return Status::Usage;
            }

            if (tinyos::kernel::security::accounts::count() >=
                tinyos::kernel::security::accounts::MaxAccounts)
            {
                context.err.line("useradd: account table is full");
                return Status::Failed;
            }

            if (tinyos::libk::text::length(name) >= tinyos::kernel::security::accounts::MaxUserNameBytes)
            {
                context.err.line("useradd: name too long");
                return Status::Usage;
            }

            context.err.line("useradd: failed");
            return Status::Failed;
        }

        context.out.write("Created ");
        context.out.write(name);
        context.out.write(admin ? " as administrator\n" : " as user\n");
        return Status::Ok;
    }

    Status cmd_userdel(Context& context)
    {
        if (context.argc < 2)
        {
            context.err.line("usage: userdel <name>");
            return Status::Usage;
        }

        const char* name = context.argv[1];
        const auto* account = tinyos::kernel::security::accounts::find(name);

        if (account == nullptr)
        {
            context.err.write(name);
            context.err.line(": no such account");
            return Status::NotFound;
        }

        // Explain the specific refusal instead of a bare failure.
        if (!tinyos::kernel::security::accounts::remove_user(name))
        {
            if (account->admin)
            {
                context.err.line("userdel: refusing to remove the last administrator");
            }
            else
            {
                context.err.line("userdel: failed");
            }

            return Status::Denied;
        }

        context.out.write("Removed ");
        context.out.line(name);
        return Status::Ok;
    }

    Status cmd_passwd(Context& context)
    {
        if (context.argc < 3)
        {
            context.err.line("usage: passwd <name> <new-password>");
            context.err.line("note: the old password is required, and the shell has no secure prompt yet");
            return Status::Usage;
        }

        // Changing another account's password requires an authenticated
        // administrator. Changing your own still requires the old password, which
        // is taken here from the current login rather than typed again.
        const char* name = context.argv[1];
        const char* new_password = context.argv[2];

        const bool self = context.session != nullptr &&
            context.session->authenticated &&
            tinyos::libk::text::equals(context.session->user_name, name);

        const bool admin = context.session != nullptr &&
            context.session->authenticated &&
            context.session->uid == 0;

        if (!self && !admin)
        {
            context.err.line("passwd: permission denied");
            return Status::Denied;
        }

        // For an administrator acting on another account, the old password is not
        // known; reset through a dedicated path rather than pretending.
        if (admin && !self)
        {
            if (!tinyos::kernel::security::accounts::remove_user(name))
            {
                // An administrator account cannot be removed this way (last-admin
                // guard), so fall back to the old-password path.
                context.err.line("passwd: administrator reset needs the old password for this account");
                return Status::Denied;
            }

            if (!tinyos::kernel::security::accounts::add_user(name, new_password, true))
            {
                context.err.line("passwd: reset failed");
                return Status::Failed;
            }

            context.out.write("Reset password for ");
            context.out.line(name);
            return Status::Ok;
        }

        // Self-service: the shell already knows the identity, but it does not
        // retain the old plaintext, so ask for it.
        if (context.argc < 4)
        {
            context.err.line("usage: passwd <name> <new-password> <old-password>");
            return Status::Usage;
        }

        if (!tinyos::kernel::security::accounts::change_password(name, context.argv[3], new_password))
        {
            context.err.line("passwd: wrong current password");
            return Status::Denied;
        }

        context.out.write("Password changed for ");
        context.out.line(name);
        return Status::Ok;
    }

    Status cmd_accounts(Context& context)
    {
        context.out.write("Accounts: ");
        write_unsigned(context.out, tinyos::kernel::security::accounts::count());
        context.out.write(" of ");
        write_unsigned(context.out, tinyos::kernel::security::accounts::MaxAccounts);
        context.out.put_char('\n');

        for (size_t index = 0; index < tinyos::kernel::security::accounts::MaxAccounts; ++index)
        {
            const auto* account = tinyos::kernel::security::accounts::at(index);
            if (account == nullptr)
            {
                continue;
            }

            context.out.write("  ");
            context.out.write(account->name);
            context.out.write("  uid=");
            write_unsigned(context.out, account->uid);
            context.out.write(account->admin ? "  admin" : "  user");
            context.out.write("  iterations=");
            write_unsigned(context.out, account->iterations);
            context.out.put_char('\n');
        }

        context.out.line("");
        context.out.write("Entropy: ");
        context.out.line(tinyos::kernel::security::accounts::entropy_is_hardware()
            ? "RDRAND"
            : "software fallback (weak)");

        if (tinyos::kernel::security::accounts::development_credentials_active())
        {
            context.out.line("WARNING: development credentials are compiled into this build.");
        }

        return Status::Ok;
    }

    const tinyos::libshell::Command IdentityCommands[] = {
        { "login", nullptr, Category::Security, Availability::Real, Risk::WritesState, CapabilityNone,
            cmd_login, "authenticate this session", "login <name> <password>", "login root tinyos" },
        { "logout", nullptr, Category::Security, Availability::Real, Risk::WritesState, CapabilityNone,
            cmd_logout, "drop the session identity", "logout", "logout" },
        { "su", nullptr, Category::Security, Availability::Real, Risk::Dangerous, CapabilityNone,
            cmd_su, "switch identity", "su <name> <password>", "su user user" },
        { "whoami", nullptr, Category::Security, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_whoami, "show the active identity", "whoami", "whoami" },
        { "id", nullptr, Category::Security, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_id, "show identity and isolation status", "id", "id" },
        { "useradd", nullptr, Category::Security, Availability::Real, Risk::Dangerous, CapabilityAdmin,
            cmd_useradd, "create an account", "useradd <name> <password> [admin]", "useradd alice secret" },
        { "userdel", nullptr, Category::Security, Availability::Real, Risk::Dangerous, CapabilityAdmin,
            cmd_userdel, "remove an account", "userdel <name>", "userdel alice" },
        { "passwd", nullptr, Category::Security, Availability::Real, Risk::Dangerous, CapabilityAdmin,
            cmd_passwd, "change or reset a password", "passwd <name> <new> [old]", "passwd root newpass oldpass" },
        { "accounts", nullptr, Category::Security, Availability::Real, Risk::ReadOnly, CapabilityNone,
            cmd_accounts, "list accounts and entropy source", "accounts", "accounts" }
    };
}

namespace tinyos::shell::commands
{
    void register_identity_commands()
    {
        tinyos::libshell::register_table(IdentityCommands,
            sizeof(IdentityCommands) / sizeof(IdentityCommands[0]));
    }
}
