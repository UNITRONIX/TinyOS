#include <stddef.h>

#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/shell/completion.hpp>

namespace
{
    const char* const Commands[] = {
        "help", "helpui", "helplist", "files", "ls", "pwd", "cd", "mkdir", "touch",
        "show", "describe", "sysinfo", "status", "syscheck", "meminfo", "uptime",
        "devices", "fbinfo", "renderinfo", "terminalinfo", "terminalstyle",
        "gfxterm", "gfxterminfo", "desktop", "desktopinfo", "clear", "reboot",
        "terminaltheme", "videomode", "installinfo", "profileinfo", "tools"
    };
}

namespace tinyos::shell::completion
{
    bool complete_prefix(const char* prefix, char* output, size_t output_size)
    {
        if (output == nullptr || output_size == 0)
        {
            return false;
        }

        output[0] = '\0';
        if (prefix == nullptr)
        {
            prefix = "";
        }

        const char* match = nullptr;
        size_t match_count = 0;
        for (size_t index = 0; index < sizeof(Commands) / sizeof(Commands[0]); ++index)
        {
            if (tinyos::core::string::starts_with(Commands[index], prefix))
            {
                match = Commands[index];
                ++match_count;
            }
        }

        if (match_count != 1 || match == nullptr)
        {
            return false;
        }

        (void)tinyos::core::memory::string_copy_safe(output, output_size, match);
        return true;
    }

    size_t command_count()
    {
        return sizeof(Commands) / sizeof(Commands[0]);
    }

    const char* command_at(size_t index)
    {
        if (index >= command_count())
        {
            return nullptr;
        }

        return Commands[index];
    }

    bool validation_self_test()
    {
        char buffer[32];
        return complete_prefix("he", buffer, sizeof(buffer)) &&
            core::string::compare(buffer, "help") == 0;
    }
}
