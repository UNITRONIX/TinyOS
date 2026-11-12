// Credential store with PBKDF2-HMAC-SHA256 password hashing.
//
// The previous implementation hashed passwords with FNV-1a into a uint32 and
// stored no salt. Its own comment called it a placeholder (defect D2). Two users
// with the same password had identical hashes, and the entire 32-bit hash space
// is small enough to enumerate.
//
// This version stores a per-account random salt and a PBKDF2 digest, compares in
// constant time, and refuses to create an account with an empty password.

#include <tinyos/arch/hal.hpp>
#include <tinyos/core/memory.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/security/accounts.hpp>
#include <tinyos/libk/sha256.hpp>
#include <tinyos/libk/text.hpp>

namespace
{
    using tinyos::kernel::security::accounts::Account;
    using tinyos::kernel::security::accounts::DefaultIterations;
    using tinyos::kernel::security::accounts::HashBytes;
    using tinyos::kernel::security::accounts::MaxAccounts;
    using tinyos::kernel::security::accounts::MaxUserNameBytes;
    using tinyos::kernel::security::accounts::SaltBytes;

    Account g_accounts[MaxAccounts] = {};
    size_t g_count = 0;
    bool g_ready = false;
    bool g_development_credentials = false;

    // Monotonic UID allocator. Self-test accounts must not consume UIDs, so the
    // value is restored around the probe rather than incremented past it.
    uint32_t g_next_uid = 1000;

    // Derives the stored digest. Kept in one place so creation and verification
    // cannot drift.
    bool derive(const char* password, const uint8_t* salt, uint32_t iterations,
        uint8_t hash[HashBytes])
    {
        if (password == nullptr || salt == nullptr || hash == nullptr)
        {
            return false;
        }

        return tinyos::libk::sha256::pbkdf2(
            password, tinyos::libk::text::length(password),
            salt, SaltBytes,
            iterations,
            hash, HashBytes);
    }

    Account* find_mutable(const char* name)
    {
        if (name == nullptr)
        {
            return nullptr;
        }

        for (size_t index = 0; index < MaxAccounts; ++index)
        {
            if (!g_accounts[index].used)
            {
                continue;
            }

            if (tinyos::libk::text::equals(g_accounts[index].name, name))
            {
                return &g_accounts[index];
            }
        }

        return nullptr;
    }

    // Counts administrators so the last one cannot be removed.
    size_t admin_count()
    {
        size_t admins = 0;
        for (size_t index = 0; index < MaxAccounts; ++index)
        {
            if (g_accounts[index].used && g_accounts[index].admin)
            {
                ++admins;
            }
        }

        return admins;
    }

    bool default_accounts_enabled()
    {
#if defined(TINYOS_ALLOW_DEVELOPMENT_CREDENTIALS)
        return true;
#else
        return false;
#endif
    }

    void clear_store()
    {
        tinyos::core::memory::set(&g_accounts, 0, sizeof(g_accounts));
        g_count = 0;
        g_next_uid = 1000;
    }
}

namespace tinyos::kernel::security::accounts
{
    void initialize()
    {
        g_ready = true;
        g_development_credentials = false;
        clear_store();

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info,
            tinyos::arch::has_hardware_entropy()
                ? "Entropy source: RDRAND."
                : "Entropy source: software fallback (NOT cryptographically strong).");

        if (!default_accounts_enabled())
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info,
                "Credential store ready (empty; development credentials disabled).");
            return;
        }

        // Development-only bootstrap identities. A release build must never ship
        // a known administrator password, so this path is compiled out unless
        // TINYOS_ALLOW_DEVELOPMENT_CREDENTIALS is defined.
        const bool root_added = add_user("root", "tinyos", true);
        const bool user_added = add_user("user", "user", false);
        g_development_credentials = root_added && user_added;

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn,
            "Credential store ready with DEVELOPMENT bootstrap credentials.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool development_credentials_active()
    {
        return g_development_credentials;
    }

    size_t count()
    {
        return g_count;
    }

    const Account* at(size_t index)
    {
        if (index >= MaxAccounts || !g_accounts[index].used)
        {
            return nullptr;
        }

        return &g_accounts[index];
    }

    const Account* find(const char* name)
    {
        return find_mutable(name);
    }

    bool add_user(const char* name, const char* password, bool admin)
    {
        if (!g_ready || name == nullptr || password == nullptr)
        {
            return false;
        }

        const size_t name_length = tinyos::libk::text::length(name);
        if (name_length == 0 || name_length >= MaxUserNameBytes)
        {
            return false;
        }

        // An empty password would authenticate against itself; refuse it rather
        // than store a credential anyone can guess.
        if (password[0] == '\0')
        {
            return false;
        }

        if (find_mutable(name) != nullptr)
        {
            return false;
        }

        if (g_count >= MaxAccounts)
        {
            return false;
        }

        // First free slot.
        Account* slot = nullptr;
        for (size_t index = 0; index < MaxAccounts; ++index)
        {
            if (!g_accounts[index].used)
            {
                slot = &g_accounts[index];
                break;
            }
        }

        if (slot == nullptr)
        {
            return false;
        }

        tinyos::core::memory::set(slot, 0, sizeof(*slot));
        tinyos::core::memory::string_copy_safe(slot->name, MaxUserNameBytes, name);

        // Fresh salt per account: identical passwords must not produce identical
        // hashes.
        tinyos::arch::random_bytes(slot->salt, SaltBytes);
        slot->iterations = DefaultIterations;

        if (!derive(password, slot->salt, slot->iterations, slot->hash))
        {
            tinyos::core::memory::set(slot, 0, sizeof(*slot));
            return false;
        }

        slot->uid = admin ? 0 : g_next_uid;
        slot->gid = admin ? 0 : 1000 + (g_next_uid - 1000);
        slot->admin = admin;
        slot->used = true;

        if (!admin)
        {
            ++g_next_uid;
        }

        ++g_count;
        return true;
    }

    bool authenticate(const char* name, const char* password)
    {
        if (!g_ready || name == nullptr || password == nullptr)
        {
            return false;
        }

        const Account* account = find_mutable(name);
        if (account == nullptr)
        {
            // Still burn a derivation so a missing user and a wrong password take
            // comparable time. Otherwise the response time reveals which account
            // names exist.
            uint8_t scratch[HashBytes];
            uint8_t dummy_salt[SaltBytes] = {};
            (void)derive(password, dummy_salt, DefaultIterations, scratch);
            return false;
        }

        uint8_t candidate[HashBytes];
        if (!derive(password, account->salt, account->iterations, candidate))
        {
            return false;
        }

        // Constant-time comparison: a byte-by-byte early exit leaks how many
        // leading hash bytes matched.
        return tinyos::libk::sha256::equal_constant_time(candidate, account->hash, HashBytes);
    }

    bool change_password(const char* name, const char* old_password, const char* new_password)
    {
        if (!g_ready || name == nullptr || old_password == nullptr || new_password == nullptr)
        {
            return false;
        }

        if (new_password[0] == '\0')
        {
            return false;
        }

        // Requiring the old password means a session cannot rotate a credential
        // it does not own.
        if (!authenticate(name, old_password))
        {
            return false;
        }

        Account* account = find_mutable(name);
        if (account == nullptr)
        {
            return false;
        }

        // New salt as well: reusing the salt would let an attacker correlate the
        // old and new hashes.
        tinyos::arch::random_bytes(account->salt, SaltBytes);
        account->iterations = DefaultIterations;

        uint8_t replacement[HashBytes];
        if (!derive(new_password, account->salt, account->iterations, replacement))
        {
            return false;
        }

        for (size_t index = 0; index < HashBytes; ++index)
        {
            account->hash[index] = replacement[index];
        }

        tinyos::core::memory::set(replacement, 0, sizeof(replacement));
        return true;
    }

    bool remove_user(const char* name)
    {
        if (!g_ready || name == nullptr)
        {
            return false;
        }

        Account* account = find_mutable(name);
        if (account == nullptr)
        {
            return false;
        }

        // Removing the last administrator would leave a system nobody can
        // administer, so refuse even when the caller has the capability.
        if (account->admin && admin_count() <= 1)
        {
            return false;
        }

        tinyos::core::memory::set(account, 0, sizeof(*account));
        if (g_count > 0)
        {
            --g_count;
        }

        return true;
    }

    void fill_random(uint8_t* buffer, size_t length)
    {
        tinyos::arch::random_bytes(buffer, length);
    }

    bool entropy_is_hardware()
    {
        return tinyos::arch::has_hardware_entropy();
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        const size_t before = g_count;

        // Self-test accounts must not consume UID numbers: a user created right
        // after boot was getting uid 1002 because the probe had already
        // incremented the allocator twice.
        const uint32_t uid_before = g_next_uid;

        // Use throwaway identities so the test does not depend on development
        // credentials being compiled in.
        const bool added = add_user("selftest", "probe-secret", false);
        const bool accepted = added && authenticate("selftest", "probe-secret");
        const bool rejected = added && !authenticate("selftest", "wrong-secret");
        const bool duplicate_rejected = added && !add_user("selftest", "probe-secret", false);
        const bool empty_rejected = !add_user("selftest2", "", false);

        // A second account with the same password must have a different hash,
        // because salts are per-account.
        const bool second_added = add_user("selftest2", "probe-secret", false);
        bool salts_differ = false;
        if (added && second_added)
        {
            const Account* first = find_mutable("selftest");
            const Account* second = find_mutable("selftest2");
            salts_differ = first != nullptr && second != nullptr &&
                !tinyos::libk::sha256::equal_constant_time(first->hash, second->hash, HashBytes);
        }

        // Password change round-trip.
        const bool changed = added && change_password("selftest", "probe-secret", "new-secret");
        const bool new_works = changed && authenticate("selftest", "new-secret");
        const bool old_rejected = changed && !authenticate("selftest", "probe-secret");

        // The last-administrator guard: with exactly one administrator present,
        // removal must be refused. The probe account is removed by the test
        // harness directly (not through remove_user) so the guard itself is what
        // is being measured, not worked around.
        bool last_admin_protected = true;
        const bool admin_added = add_user("selftest-admin", "probe-secret", true);
        if (admin_added)
        {
            if (admin_count() == 1)
            {
                // Sole administrator: removal must fail.
                last_admin_protected = !remove_user("selftest-admin");
            }
            else
            {
                // Another administrator exists, so this one is removable.
                last_admin_protected = remove_user("selftest-admin");
            }

            // Unwind without going through the guard, because the guard is
            // precisely what just refused for the sole-admin case.
            Account* probe = find_mutable("selftest-admin");
            if (probe != nullptr)
            {
                tinyos::core::memory::set(probe, 0, sizeof(*probe));
                if (g_count > 0)
                {
                    --g_count;
                }
            }
        }

        (void)remove_user("selftest");
        (void)remove_user("selftest2");

        // Restore the allocator so probe accounts leave no trace in the UID
        // space.
        g_next_uid = uid_before;

        const bool count_restored = g_count == before;

        // Report which precondition failed. A bare "false" from this function
        // produced a panic with no indication of the cause, which cost a boot
        // cycle per guess.
        if (!added)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: self-test could not add an account");
        }
        else if (!accepted)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: correct password rejected");
        }
        else if (!rejected)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: wrong password accepted");
        }
        else if (!duplicate_rejected)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: duplicate name accepted");
        }
        else if (!empty_rejected)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: empty password accepted");
        }
        else if (!salts_differ)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: identical hashes for identical passwords");
        }
        else if (!changed)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: password change failed");
        }
        else if (!new_works)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: new password rejected");
        }
        else if (!old_rejected)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: old password still accepted");
        }
        else if (!last_admin_protected)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, "accounts: last administrator was removable");
        }
        else if (!count_restored)
        {
            char buffer[96];
            tinyos::core::memory::string_copy_safe(buffer, sizeof(buffer), "accounts: count ");
            size_t used = tinyos::libk::text::length(buffer);

            char digits[21];
            size_t position = sizeof(digits);
            digits[--position] = '\0';
            size_t value = before;
            do
            {
                digits[--position] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0 && position > 0);

            for (size_t index = position; digits[index] != '\0'; ++index)
            {
                buffer[used] = digits[index];
                ++used;
            }
            buffer[used] = '\0';

            tinyos::core::memory::string_copy_safe(buffer + used, sizeof(buffer) - used, " -> ");
            used = tinyos::libk::text::length(buffer);

            position = sizeof(digits);
            digits[--position] = '\0';
            value = g_count;
            do
            {
                digits[--position] = static_cast<char>('0' + (value % 10));
                value /= 10;
            } while (value != 0 && position > 0);

            for (size_t index = position; digits[index] != '\0'; ++index)
            {
                buffer[used] = digits[index];
                ++used;
            }
            buffer[used] = '\0';

            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Error, buffer);
        }

        return accepted &&
            rejected &&
            duplicate_rejected &&
            empty_rejected &&
            salts_differ &&
            changed &&
            new_works &&
            old_rejected &&
            last_admin_protected &&
            count_restored;
    }
}
