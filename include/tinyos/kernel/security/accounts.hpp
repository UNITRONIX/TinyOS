#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::security::accounts
{
    // Credential store.
    //
    // The previous implementation hashed passwords with FNV-1a into a uint32 and
    // its own comment admitted it was "a placeholder hash used only to keep the
    // account contract testable. It is not a password KDF" (defect D2). It also
    // stored no salt, so two users with the same password had the same hash and
    // a precomputed table for the whole 32-bit space was trivially small.
    //
    // This version uses PBKDF2-HMAC-SHA256 with a per-account random salt and a
    // configurable iteration count, which is the same construction the host
    // tooling can verify.

    inline constexpr size_t MaxUserNameBytes = 32;
    inline constexpr size_t SaltBytes = 16;
    inline constexpr size_t HashBytes = 32;

    // Iteration count for new hashes. 10_000 is a deliberate floor: it is slow
    // enough to be a real cost per guess on this hardware and fast enough that
    // authenticating does not stall the shell visibly.
    inline constexpr uint32_t DefaultIterations = 10000;

    // Hard ceiling on stored accounts. The table is static because there is no
    // dynamic allocator available at credential-store initialisation time.
    inline constexpr size_t MaxAccounts = 16;

    struct Account
    {
        char name[MaxUserNameBytes];
        uint8_t salt[SaltBytes];
        uint8_t hash[HashBytes];
        uint32_t iterations;
        uint32_t uid;
        uint32_t gid;
        bool admin;
        bool used;
    };

    void initialize();
    bool is_ready();

    // Development credentials are compiled in only when
    // TINYOS_ALLOW_DEVELOPMENT_CREDENTIALS is defined. A release build must
    // never ship a known administrator password.
    bool development_credentials_active();

    size_t count();
    const Account* at(size_t index);
    const Account* find(const char* name);

    // Creates an account. Fails on a duplicate name, a full table, a name that
    // does not fit, or an empty password. Returns false rather than silently
    // refusing, so the caller can report why.
    bool add_user(const char* name, const char* password, bool admin);

    // Verifies a password. Uses a constant-time comparison so a timing
    // measurement cannot narrow down the hash.
    bool authenticate(const char* name, const char* password);

    // Replaces an account's password. Requires the old password, so a session
    // cannot silently rotate someone else's credential.
    bool change_password(const char* name, const char* old_password, const char* new_password);

    // Removes an account. The last administrator cannot be removed: that would
    // leave a system nobody can administer.
    bool remove_user(const char* name);

    // Fills `buffer` with cryptographically-usable random bytes.
    //
    // Backed by RDRAND when the CPU reports it, mixed with the TSC and a counter
    // otherwise. The fallback is explicitly weak and says so through
    // entropy_is_hardware() so callers can report the difference.
    void fill_random(uint8_t* buffer, size_t length);
    bool entropy_is_hardware();

    bool validation_self_test();
}
