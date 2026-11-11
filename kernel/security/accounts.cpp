#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/security/accounts.hpp>

namespace
{
    constexpr size_t MaxAccounts = 8;
    constexpr size_t MaxName = 24;

    char g_names[MaxAccounts][MaxName] = {};
    tinyos::kernel::security::accounts::Account g_accounts[MaxAccounts] = {};
    size_t g_count = 0;
    bool g_ready = false;
    bool g_default_accounts_loaded = false;

    size_t copy_name(char* destination, size_t capacity, const char* source)
    {
        size_t index = 0;
        while (source != nullptr && source[index] != '\0' && index + 1 < capacity)
        {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
        return index;
    }

    void clear_store()
    {
        for (size_t index = 0; index < MaxAccounts; ++index)
        {
            g_names[index][0] = '\0';
            g_accounts[index].name = nullptr;
            g_accounts[index].password_hash = 0;
            g_accounts[index].admin = false;
        }

        g_count = 0;
    }
}

namespace tinyos::kernel::security::accounts
{
    uint32_t hash_password(const char* password)
    {
        // NOTE: FNV-1a is a placeholder hash used only to keep the account
        // contract testable. It is not a password KDF. Phase F replaces this
        // with Argon2id/PBKDF2 over a per-account salt before any installed
        // system stores real credentials.
        uint32_t hash = 2166136261u;
        if (password == nullptr)
        {
            return hash;
        }

        for (size_t index = 0; password[index] != '\0'; ++index)
        {
            hash ^= static_cast<uint8_t>(password[index]);
            hash *= 16777619u;
        }

        return hash;
    }

    bool default_accounts_enabled()
    {
#if defined(TINYOS_ALLOW_DEVELOPMENT_CREDENTIALS)
        return true;
#else
        return false;
#endif
    }

    void initialize()
    {
        g_ready = true;
        g_default_accounts_loaded = false;
        clear_store();

        if (!default_accounts_enabled())
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Info,
                "Account store ready (empty; development credentials disabled).");
            return;
        }

        // Development-only bootstrap identities. A release build must never
        // ship a known administrator password, so this path is compiled out
        // unless TINYOS_ALLOW_DEVELOPMENT_CREDENTIALS is defined.
        (void)add_user("root", "tinyos", true);
        (void)add_user("user", "user", false);
        g_default_accounts_loaded = g_count == 2;
        tinyos::kernel::klog::write_line(
            tinyos::kernel::klog::Level::Warn,
            "Account store ready with DEVELOPMENT bootstrap credentials.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool development_credentials_active()
    {
        return g_default_accounts_loaded;
    }

    size_t count()
    {
        return g_count;
    }

    const Account* at(size_t index)
    {
        return index < g_count ? &g_accounts[index] : nullptr;
    }

    bool add_user(const char* name, const char* password, bool admin)
    {
        if (!g_ready || name == nullptr || password == nullptr || name[0] == '\0' || g_count >= MaxAccounts)
        {
            return false;
        }

        for (size_t index = 0; index < g_count; ++index)
        {
            size_t n = 0;
            while (g_names[index][n] != '\0' && name[n] != '\0' && g_names[index][n] == name[n])
            {
                ++n;
            }

            if (g_names[index][n] == '\0' && name[n] == '\0')
            {
                return false;
            }
        }

        copy_name(g_names[g_count], MaxName, name);
        g_accounts[g_count].name = g_names[g_count];
        g_accounts[g_count].password_hash = hash_password(password);
        g_accounts[g_count].admin = admin;
        ++g_count;
        return true;
    }

    bool authenticate(const char* name, const char* password)
    {
        if (!g_ready || name == nullptr || password == nullptr)
        {
            return false;
        }

        const uint32_t hash = hash_password(password);
        for (size_t index = 0; index < g_count; ++index)
        {
            size_t n = 0;
            while (g_accounts[index].name[n] != '\0' && name[n] != '\0' && g_accounts[index].name[n] == name[n])
            {
                ++n;
            }

            if (g_accounts[index].name[n] == '\0' && name[n] == '\0')
            {
                return g_accounts[index].password_hash == hash;
            }
        }

        return false;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        // Validate the mechanism on a throwaway identity so the test does not
        // depend on development credentials being compiled in.
        const size_t count_before = g_count;
        if (!add_user("selftest", "probe-secret", false))
        {
            return false;
        }

        const bool accepted = authenticate("selftest", "probe-secret");
        const bool rejected = !authenticate("selftest", "wrong-secret");
        const bool duplicate_rejected = !add_user("selftest", "probe-secret", false);

        // Roll the probe account back out of the table.
        if (g_count == count_before + 1)
        {
            const size_t slot = g_count - 1;
            g_names[slot][0] = '\0';
            g_accounts[slot].name = nullptr;
            g_accounts[slot].password_hash = 0;
            g_accounts[slot].admin = false;
            --g_count;
        }

        return accepted &&
            rejected &&
            duplicate_rejected &&
            g_count == count_before;
    }
}
