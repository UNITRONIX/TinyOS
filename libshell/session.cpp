#include <tinyos/core/memory.hpp>
#include <tinyos/libk/path.hpp>
#include <tinyos/libk/text.hpp>
#include <tinyos/libshell/session.hpp>

namespace
{
    using tinyos::libshell::Environment;
    using tinyos::libshell::MaxEnvEntries;
    using tinyos::libshell::MaxEnvName;
    using tinyos::libshell::MaxEnvValue;
}

namespace tinyos::libshell
{
    void Environment::clear()
    {
        for (size_t index = 0; index < MaxEnvEntries; ++index)
        {
            m_entries[index].used = false;
            m_entries[index].name[0] = '\0';
            m_entries[index].value[0] = '\0';
        }

        m_count = 0;
    }

    bool Environment::set(const char* name, const char* value)
    {
        if (name == nullptr || name[0] == '\0')
        {
            return false;
        }

        if (tinyos::libk::text::length(name) >= MaxEnvName)
        {
            return false;
        }

        if (value != nullptr && tinyos::libk::text::length(value) >= MaxEnvValue)
        {
            return false;
        }

        for (size_t index = 0; index < MaxEnvEntries; ++index)
        {
            if (m_entries[index].used && tinyos::libk::text::equals(m_entries[index].name, name))
            {
                tinyos::core::memory::string_copy_safe(m_entries[index].value, MaxEnvValue, value);
                return true;
            }
        }

        for (size_t index = 0; index < MaxEnvEntries; ++index)
        {
            if (!m_entries[index].used)
            {
                tinyos::core::memory::string_copy_safe(m_entries[index].name, MaxEnvName, name);
                tinyos::core::memory::string_copy_safe(m_entries[index].value, MaxEnvValue, value);
                m_entries[index].used = true;
                ++m_count;
                return true;
            }
        }

        return false;
    }

    const char* Environment::get(const char* name) const
    {
        if (name == nullptr)
        {
            return nullptr;
        }

        for (size_t index = 0; index < MaxEnvEntries; ++index)
        {
            if (m_entries[index].used && tinyos::libk::text::equals(m_entries[index].name, name))
            {
                return m_entries[index].value;
            }
        }

        return nullptr;
    }

    bool Environment::unset(const char* name)
    {
        if (name == nullptr)
        {
            return false;
        }

        for (size_t index = 0; index < MaxEnvEntries; ++index)
        {
            if (m_entries[index].used && tinyos::libk::text::equals(m_entries[index].name, name))
            {
                m_entries[index].used = false;
                m_entries[index].name[0] = '\0';
                m_entries[index].value[0] = '\0';
                if (m_count > 0)
                {
                    --m_count;
                }
                return true;
            }
        }

        return false;
    }

    const char* Environment::name_at(size_t index) const
    {
        size_t seen = 0;
        for (size_t slot = 0; slot < MaxEnvEntries; ++slot)
        {
            if (!m_entries[slot].used)
            {
                continue;
            }

            if (seen == index)
            {
                return m_entries[slot].name;
            }

            ++seen;
        }

        return nullptr;
    }

    const char* Environment::value_at(size_t index) const
    {
        size_t seen = 0;
        for (size_t slot = 0; slot < MaxEnvEntries; ++slot)
        {
            if (!m_entries[slot].used)
            {
                continue;
            }

            if (seen == index)
            {
                return m_entries[slot].value;
            }

            ++seen;
        }

        return nullptr;
    }

    // Capability masks live in the header so the kernel and host tests agree.

    void initialize_session(Session& session)
    {
        tinyos::core::memory::set(&session, 0, sizeof(session));
        tinyos::core::memory::string_copy_safe(session.cwd, sizeof(session.cwd), "/");

        // The development shell starts as an unauthenticated root identity, but
        // with only the unauthenticated capability set. `whoami` reports
        // "(unauthenticated)" and administrative commands are refused until a
        // real login happens. uid stays 0 so file ownership checks have a value;
        // `authenticated` is what gates privileges.
        session.uid = 0;
        session.gid = 0;
        session.capabilities = UnauthenticatedCapabilities;
        tinyos::core::memory::string_copy_safe(session.user_name, sizeof(session.user_name), "root");
        session.authenticated = false;
        session.interactive = true;

        session.env.clear();
        session.env.set("USER", "root");
        session.env.set("PWD", "/");
        session.env.set("SHELL", "tinysh");
        session.env.set("HOME", "/");
        session.env.set("PATH", "/system");
    }

    bool is_root(const Session& session)
    {
        return session.uid == 0;
    }

    void set_session_cwd(Session& session, const char* path)
    {
        if (path == nullptr || path[0] != '/')
        {
            return;
        }

        tinyos::core::memory::string_copy_safe(session.cwd, sizeof(session.cwd), path);

        // $PWD is derived state; keeping it in step here means no caller has to
        // remember to update it separately.
        session.env.set("PWD", session.cwd);
    }
}
