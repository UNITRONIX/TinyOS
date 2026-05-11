#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/admin/tools.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    using tinyos::kernel::admin::tools::Category;
    using tinyos::kernel::admin::tools::State;
    using tinyos::kernel::admin::tools::Tool;

    const Tool g_tools[] = {
        { "help", "show shell commands", Category::Shell, State::Ready, false, false },
        { "tools", "list management tools", Category::Shell, State::Ready, false, false },
        { "toolinfo", "show management tool manifest summary", Category::Shell, State::Ready, false, false },
        { "tool", "show one management tool", Category::Shell, State::Ready, false, false },
        { "aliases", "show compatibility aliases", Category::Shell, State::Ready, false, false },
        { "clear", "clear console screen", Category::Shell, State::Ready, true, false },
        { "files", "list RAMFS directory", Category::Files, State::Ready, false, false },
        { "fsmap", "show RAMFS tree", Category::Files, State::Ready, false, false },
        { "show", "print RAMFS file", Category::Files, State::Ready, false, false },
        { "describe", "show RAMFS node metadata", Category::Files, State::Ready, false, false },
        { "write", "overwrite writable RAMFS file", Category::Files, State::Ready, true, true },
        { "ramfsinfo", "show RAMFS state", Category::Files, State::Ready, false, false },
        { "vfsinfo", "show VFS state", Category::Files, State::Ready, false, false },
        { "blockinfo", "show RAM block device", Category::Storage, State::Ready, false, false },
        { "storageinfo", "show block VFS mount", Category::Storage, State::Ready, false, false },
        { "devices", "list device registry", Category::Devices, State::Ready, false, false },
        { "device", "show one device", Category::Devices, State::Ready, false, false },
        { "fbinfo", "show framebuffer surface", Category::Devices, State::Ready, false, false },
        { "meminfo", "show parsed memory map", Category::Memory, State::Ready, false, false },
        { "frameinfo", "show physical frame allocator", Category::Memory, State::Ready, false, false },
        { "heapinfo", "show kernel heap", Category::Memory, State::Ready, false, false },
        { "heaptest", "run heap self-test", Category::Memory, State::Ready, true, false },
        { "paginginfo", "show paging scaffold", Category::Memory, State::Ready, false, false },
        { "addrspaceinfo", "show address-space scaffold", Category::Memory, State::Ready, false, false },
        { "runtimeinfo", "show language runtime manifest", Category::Runtime, State::Ready, false, false },
        { "appinfo", "show app capability profiles", Category::Runtime, State::Ready, false, false },
        { "launchinfo", "show launch policy counters", Category::Runtime, State::Ready, false, false },
        { "launchcheck", "dry-check app launch policy", Category::Runtime, State::Ready, true, false },
        { "tappinfo", "show TAPP package registry summary", Category::Runtime, State::Ready, false, false },
        { "tapps", "list TAPP packages", Category::Runtime, State::Ready, false, false },
        { "tapp", "show one TAPP package", Category::Runtime, State::Ready, false, false },
        { "tappcheck", "dry-check TAPP launch readiness", Category::Runtime, State::Ready, true, false },
        { "tappverify", "verify TAPP install gate policy", Category::Security, State::Ready, true, false },
        { "trustinfo", "show TAPP trust store", Category::Security, State::Ready, false, false },
        { "trust", "show one TAPP trust anchor", Category::Security, State::Ready, false, false },
        { "imageinfo", "show secure image pipeline", Category::Runtime, State::Ready, false, false },
        { "provisioninfo", "show provisioning workflow", Category::Runtime, State::Ready, false, false },
        { "deployinfo", "show remote deployment plan", Category::Runtime, State::Ready, false, false },
        { "sysinfo", "show syscall ABI scaffold", Category::Runtime, State::Ready, false, false },
        { "userinfo", "show user transition scaffold", Category::Runtime, State::Ready, false, false },
        { "elfinfo", "show ELF loader scaffold", Category::Runtime, State::Ready, false, false },
        { "modulesinfo", "show boot modules", Category::Runtime, State::Ready, false, false },
        { "securityinfo", "show security scaffold", Category::Security, State::Ready, false, false },
        { "integritycheck", "run allocator integrity check", Category::Security, State::Ready, true, false },
        { "requirements", "show platform requirements", Category::Security, State::Ready, false, false },
        { "platforminfo", "show platform compatibility manifest", Category::Devices, State::Ready, false, false },
        { "pcinfo", "show PC platform init contract", Category::Devices, State::Ready, false, false },
        { "archinfo", "show architecture capability manifest", Category::Devices, State::Ready, false, false },
        { "renderinfo", "show renderer state", Category::Ui, State::Ready, false, false },
        { "cursorinfo", "show cursor scaffold state", Category::Ui, State::Ready, false, false },
        { "terminalinfo", "show terminal UI state", Category::Ui, State::Ready, false, false },
        { "widgetinfo", "show widget state", Category::Ui, State::Ready, false, false },
        { "wminfo", "show window manager state", Category::Ui, State::Ready, false, false },
        { "desktopinfo", "show desktop shell state", Category::Ui, State::Ready, false, false },
        { "uieventinfo", "show UI event queue", Category::Ui, State::Ready, false, false },
        { "inputinfo", "show input queue", Category::Ui, State::Ready, false, false },
        { "keyboardinfo", "show keyboard driver", Category::Ui, State::Ready, false, false },
        { "schedinfo", "show scheduler scaffold", Category::Scheduling, State::Ready, false, false },
        { "taskinfo", "show kernel tasks", Category::Scheduling, State::Ready, false, false },
        { "contextinfo", "show context switch ABI", Category::Scheduling, State::Ready, false, false },
        { "timerinfo", "show PIT timer", Category::Scheduling, State::Ready, false, false },
        { "uptime", "show PIT ticks", Category::Scheduling, State::Ready, false, false },
        { "reboot", "reboot machine", Category::Power, State::Ready, true, true },
        { "int3", "trigger breakpoint exception", Category::Development, State::Ready, true, true },
        { "panic", "trigger kernel panic", Category::Development, State::Ready, true, true },
        { "copy", "copy files", Category::Files, State::Planned, true, false },
        { "remove", "remove files", Category::Files, State::Planned, true, true },
        { "mkdir", "create directories", Category::Files, State::Planned, true, false },
        { "mount", "mount storage volumes", Category::Storage, State::Planned, true, true },
        { "ps", "list user processes", Category::Scheduling, State::Planned, false, false },
        { "kill", "stop a process", Category::Scheduling, State::Planned, true, true },
        { "service", "manage system services", Category::Scheduling, State::Planned, true, true },
        { "useradd", "create user identity", Category::Security, State::Planned, true, true },
        { "chmod", "change access policy", Category::Security, State::Planned, true, true },
        { "package", "manage system packages", Category::Development, State::Planned, true, true },
        { "tappinstall", "install a TAPP package", Category::Development, State::Planned, true, true },
        { "tappremove", "remove a TAPP package", Category::Development, State::Planned, true, true },
        { "imagebuild", "build a bootable TinyOS image", Category::Development, State::Planned, true, false },
        { "imagesign", "sign a TinyOS image or manifest", Category::Security, State::Planned, true, true },
        { "imageencrypt", "encrypt a TinyOS image for a target", Category::Security, State::Planned, true, true },
        { "keygen", "create deployment keys", Category::Security, State::Planned, true, true },
        { "deploy", "send image through a remote transport", Category::Development, State::Planned, true, true },
        { "provision", "activate an image on a target", Category::Development, State::Planned, true, true },
        { "rollback", "restore previous image slot", Category::Power, State::Planned, true, true },
        { "netinfo", "show network state", Category::Devices, State::Planned, false, false }
    };

    bool g_ready = false;

    bool strings_equal(const char* left, const char* right)
    {
        if (left == nullptr || right == nullptr)
        {
            return left == right;
        }

        while (*left != '\0' && *right != '\0')
        {
            if (*left != *right)
            {
                return false;
            }

            ++left;
            ++right;
        }

        return *left == '\0' && *right == '\0';
    }
}

namespace tinyos::kernel::admin::tools
{
    void initialize()
    {
        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "System management tools manifest initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    size_t count()
    {
        return sizeof(g_tools) / sizeof(g_tools[0]);
    }

    size_t ready_count()
    {
        size_t ready = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_tools[index].state == State::Ready)
            {
                ++ready;
            }
        }

        return ready;
    }

    size_t planned_count()
    {
        size_t planned = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_tools[index].state == State::Planned)
            {
                ++planned;
            }
        }

        return planned;
    }

    size_t write_tool_count()
    {
        size_t writes = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_tools[index].writes_state)
            {
                ++writes;
            }
        }

        return writes;
    }

    size_t high_risk_count()
    {
        size_t risky = 0;
        for (size_t index = 0; index < count(); ++index)
        {
            if (g_tools[index].high_risk)
            {
                ++risky;
            }
        }

        return risky;
    }

    const Tool* at(size_t index)
    {
        return index < count() ? &g_tools[index] : nullptr;
    }

    const Tool* find(const char* command)
    {
        for (size_t index = 0; index < count(); ++index)
        {
            if (strings_equal(g_tools[index].command, command))
            {
                return &g_tools[index];
            }
        }

        return nullptr;
    }

    bool validation_self_test()
    {
        return g_ready &&
            count() >= 50 &&
            ready_count() >= 40 &&
            planned_count() >= 8 &&
            write_tool_count() >= 8 &&
            high_risk_count() >= 5 &&
            find("files") != nullptr &&
            find("runtimeinfo") != nullptr &&
            find("appinfo") != nullptr &&
            find("launchcheck") != nullptr &&
            find("tappinfo") != nullptr &&
            find("tappverify") != nullptr &&
            find("trustinfo") != nullptr &&
            find("imageinfo") != nullptr &&
            find("imageencrypt") != nullptr &&
            find("deploy") != nullptr &&
            find("tools") != nullptr &&
            find("mount") != nullptr;
    }

    const char* category_name(Category category)
    {
        switch (category)
        {
        case Category::Shell:
            return "shell";
        case Category::Files:
            return "files";
        case Category::Storage:
            return "storage";
        case Category::Devices:
            return "devices";
        case Category::Memory:
            return "memory";
        case Category::Runtime:
            return "runtime";
        case Category::Security:
            return "security";
        case Category::Ui:
            return "ui";
        case Category::Scheduling:
            return "scheduling";
        case Category::Power:
            return "power";
        case Category::Development:
            return "development";
        }

        return "unknown";
    }

    const char* state_name(State state)
    {
        switch (state)
        {
        case State::Ready:
            return "ready";
        case State::Planned:
            return "planned";
        }

        return "unknown";
    }
}