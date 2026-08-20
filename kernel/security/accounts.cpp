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
}

namespace tinyos::kernel::security::accounts
{
    uint32_t hash_password(const char* password)
    {
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

    void initialize()
    {
        g_count = 0;
        g_ready = true;
        (void)add_user("root", "tinyos", true);
        (void)add_user("user", "user", false);
        tinyos::kernel::klog::write_line(
            tinyos::kernel::klog::Level::Info,
            "Account store ready (root/user bootstrap hashes).");
    }

    bool is_ready()
    {
        return g_ready;
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
        if (!g_ready || name == nullptr || password == nullptr || g_count >= MaxAccounts)
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
        return g_ready &&
            g_count >= 2 &&
            authenticate("root", "tinyos") &&
            !authenticate("root", "wrong") &&
            authenticate("user", "user");
    }
}
