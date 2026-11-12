#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::libshell
{
    // Environment for $NAME expansion and command context.
    //
    // Fixed capacity, no allocation: the shell must link into both the kernel
    // and a host test binary.
    inline constexpr size_t MaxEnvEntries = 16;
    inline constexpr size_t MaxEnvName = 32;
    inline constexpr size_t MaxEnvValue = 96;

    class Environment
    {
    public:
        void clear();
        bool set(const char* name, const char* value);
        const char* get(const char* name) const;
        bool unset(const char* name);
        size_t count() const { return m_count; }
        const char* name_at(size_t index) const;
        const char* value_at(size_t index) const;

    private:
        struct Entry
        {
            char name[MaxEnvName];
            char value[MaxEnvValue];
            bool used;
        };

        Entry m_entries[MaxEnvEntries];
        size_t m_count = 0;
    };

    inline constexpr size_t MaxUserName = 32;
    inline constexpr size_t MaxCwdBytes = 128;

    // Capability bits granted to an unauthenticated session.
    //
    // An unauthenticated session gets read access and a shell, nothing more. The
    // first build granted the full mask here, which meant `useradd` worked
    // without anyone logging in — the store had real authentication and the
    // shell simply ignored it. That is a privilege-escalation hole, not a
    // convenience (defect D48).
    inline constexpr uint32_t UnauthenticatedCapabilities = (1u << 0) | (1u << 1);

    // Capabilities granted to an authenticated non-administrator: console, file
    // read, file write, device access. No process control, no admin operations.
    inline constexpr uint32_t UserCapabilities = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 4);

    // Every capability bit. Administrators receive this.
    inline constexpr uint32_t AdminCapabilities = 0xFFFFFFFFu;

    // Session state that must survive between command lines but must not be
    // global.
    //
    // Two consoles differ only by which Session they point at. Before this
    // existed the shell kept its working directory in one file-scope variable
    // (shell/shell.cpp:87), so a second session was impossible by construction
    // (defect D5).
    //
    // Commands receive a pointer to the session because some of them legitimately
    // change it: `cd` moves the working directory, `set`/`unset` edit the
    // environment. Passing it by value forced the earlier `cd` to refuse with
    // "needs the session object", which was honest but useless.
    struct Session
    {
        char cwd[MaxCwdBytes];
        Environment env;

        uint32_t uid;
        uint32_t gid;
        uint32_t capabilities;

        // Display name of the active user. Copied rather than pointed at so the
        // session does not depend on the account store's lifetime.
        char user_name[MaxUserName];

        // False until a real login established this identity. Until the login
        // path exists (M2c) every session starts unauthenticated, and `whoami`
        // says so instead of implying a verified root (defect D3).
        bool authenticated;

        // Whether the session has a real terminal. Full-screen interactive
        // commands refuse when this is false.
        bool interactive;
    };

    // Resets a session to the documented default: root working directory, no
    // authenticated identity, full capabilities for development.
    void initialize_session(Session& session);

    // True when the session carries the root identity (uid 0).
    bool is_root(const Session& session);

    // Applies the session's identity to CWD after a directory change, keeping
    // $PWD consistent with the prompt.
    void set_session_cwd(Session& session, const char* path);
}
