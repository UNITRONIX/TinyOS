#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>

namespace
{
    constexpr char ReadmeText[] = "TinyOS RAMFS\nUse files, fsmap, show, describe and write. Use tools for the management command manifest. Compatibility aliases are optional.\n";
    constexpr char ShellText[] = "kernel-shell=active\nfuture-userland-shell=planned\n";
    constexpr char SystemText[] = "name=TinyOS\narch=i686\nstorage=ramfs\n";
    constexpr char RequirementsText[] = "arch=i686\nboot=grub-multiboot-iso\nminimum-ram=32MiB\nrecommended-ram=128MiB\ndisplay=vga-text-80x25\ninput=ps2-keyboard\nemulator=qemu-system-i386\n";
    constexpr char RuntimeText[] = "active=native-c-elf32,native-cpp-elf32\nabi=tinyos-native-i686-v0\nplanned=wasm32-sandbox,tiny-bytecode,tiny-script\nsecurity=capability-gated\nselfhost=planned-native-toolchain\n";
    constexpr char AppsText[] = "ready=system-shell\nplanned=example-system-tool,desktop-shell,web-gui-host,selfhost-toolchain,bytecode-service\npolicy=runtime-capability-subset\nlauncher=profile-check-only\nsecurity=least-privilege-before-launch\ncommands=appinfo,launchinfo,launchcheck\n";
    constexpr char TappText[] = "extension=.tapp\nformat=tinyos-tapp-v0\nsignature=detached-rsa-sha256\nreceipt=.sig.receipt\nready=system-shell.tapp\nvalid=example-system-tool.tapp\nplanned=desktop-shell.tapp,web-gui-host.tapp,selfhost-toolchain.tapp\ncommands=tappinfo,tapps,tapp,tappcheck,tappverify\nhost-tools=keygen-app,trust-app,sign-app,verify-app\npolicy=signed-manifest-required,payload-hash-required,capability-subset-required\nverifier=install-gate-contract\n";
    constexpr char TrustText[] = "store=tapp-trust-v0\nready=tinyos-dev-app-signing\nplanned=tinyos-release-app-root,tinyos-image-signing-root,tinyos-recovery-root\nrevoked=tinyos-revoked-test-key\nalgorithm=rsa-sha256\ndev-key=build/keys/tapp-dev-public.pem\nhost-tool=scripts/tinyos-image.sh trust-app\ncommands=trustinfo,trust\npolicy=development-key-not-for-release,revoked-keys-never-match\n";
    constexpr char ToolsText[] = "ready=help,tools,toolinfo,tool,files,fsmap,show,describe,write,devices,device,blockinfo,storageinfo,meminfo,frameinfo,heapinfo,paginginfo,addrspaceinfo,runtimeinfo,appinfo,launchinfo,launchcheck,tappinfo,tapps,tapp,tappcheck,tappverify,trustinfo,trust,imageinfo,provisioninfo,deployinfo,securityinfo,integritycheck,renderinfo,terminalinfo,widgetinfo,uieventinfo,schedinfo,taskinfo,timerinfo,uptime,reboot\nplanned=copy,remove,mkdir,mount,ps,kill,service,useradd,chmod,package,tappinstall,tappremove,imagebuild,imagesign,imageencrypt,keygen,deploy,provision,rollback,netinfo\npolicy=high-risk-tools-must-be-explicit\n";
    constexpr char ProvisioningText[] = "pipeline=app-bundle,app-signature,system-profile,image-manifest,image-build,image-sign,image-encrypt,deploy-check,deploy-ssh,target-verify,rollback-slot\nhost-tool=scripts/tinyos-image.sh\nready-contracts=app-bundle,app-signature,system-profile,image-manifest,deploy-check,deploy-receipt\nplanned-host-tools=imagebuild,imagesign,imageencrypt,keygen,deploy\nplanned-kernel=target-verify,rollback-slot\ntransport=ssh-sftp-now,tinylink-later\npolicy=sign-before-deploy,encrypt-per-target,deploy-check-before-transport,rollback-before-activate\n";
    constexpr char UiText[] = "renderer=text-grid\nrenderer-primitives=fill-rect,clear-area\nterminal=status-row-plus-content\nterminal-panels=clear,panel\nwidgets=label,button\nwidget-events=dispatch,activate\nevents=ui-event-queue\ncommands=renderinfo,rendertest,renderfilltest,terminalinfo,terminaltest,terminalclear,terminalpaneltest,widgetinfo,widgettest,widgetdispatch,widgetactiontest,uieventinfo,uieventpump,uieventpeek,uieventtest\n";
    constexpr char ConsoleText[] = "console device placeholder\n";
    constexpr char BlockDeviceText[] = "name=ram-block0\nclass=block\nsector-size=512\nsectors=8\nwritable=true\n";
    constexpr char FramebufferDeviceText[] = "name=vga-text-grid\nclass=framebuffer\nkind=text-grid\nwidth=80\nheight=25\n";
    constexpr char UiEventDeviceText[] = "name=ui-event-queue\nclass=input\nsource=input-queue\ncapacity=64\n";
    constexpr char NotesInitialText[] = "TinyOS editable RAMFS note.\n";
    constexpr char ExampleTappText[] = "tinyos.tapp.version=0\ntinyos.tapp.kind=manifest-envelope\ntinyos.tapp.source_sha256=example-not-signed-yet\ntinyos.tapp.signature_policy=required\ntinyos.tapp.signature_state=unsigned\ntinyos.tapp.payload_policy=hash-required\ntinyos.tapp.payload_state=external\napp.name=example-system-tool\napp.runtime=native-cpp-elf32\napp.profile=example-system-tool\napp.entry=/apps/example-system-tool.elf\napp.capabilities=console,file-read,clock\napp.trust=developer-signed\nstate=valid-manifest\nverify=signature-required\n";

    char g_notes_buffer[512] = "TinyOS editable RAMFS note.\n";

    tinyos::kernel::vfs::Node g_root = { "/", true, nullptr, nullptr, 0, 0, false, nullptr };
    tinyos::kernel::vfs::Node g_apps = { "apps", true, nullptr, nullptr, 0, 0, false, &g_root };
    tinyos::kernel::vfs::Node g_system = { "system", true, nullptr, nullptr, 0, 0, false, &g_root };
    tinyos::kernel::vfs::Node g_devices = { "devices", true, nullptr, nullptr, 0, 0, false, &g_root };
    tinyos::kernel::vfs::Node g_users = { "users", true, nullptr, nullptr, 0, 0, false, &g_root };
    tinyos::kernel::vfs::Node g_readme = { "readme.txt", false, ReadmeText, nullptr, sizeof(ReadmeText) - 1, 0, false, &g_root };
    tinyos::kernel::vfs::Node g_shell_info = { "shell.txt", false, ShellText, nullptr, sizeof(ShellText) - 1, 0, false, &g_apps };
    tinyos::kernel::vfs::Node g_system_info = { "system.txt", false, SystemText, nullptr, sizeof(SystemText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_requirements_info = { "requirements.txt", false, RequirementsText, nullptr, sizeof(RequirementsText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_runtime_info = { "runtimes.txt", false, RuntimeText, nullptr, sizeof(RuntimeText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_apps_info = { "apps.txt", false, AppsText, nullptr, sizeof(AppsText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_tapp_info = { "tapp.txt", false, TappText, nullptr, sizeof(TappText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_trust_info = { "trust.txt", false, TrustText, nullptr, sizeof(TrustText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_tools_info = { "tools.txt", false, ToolsText, nullptr, sizeof(ToolsText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_provisioning_info = { "provisioning.txt", false, ProvisioningText, nullptr, sizeof(ProvisioningText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_ui_info = { "ui.txt", false, UiText, nullptr, sizeof(UiText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_console = { "console", false, ConsoleText, nullptr, sizeof(ConsoleText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_example_tapp = { "example-system-tool.tapp", false, ExampleTappText, nullptr, sizeof(ExampleTappText) - 1, 0, false, &g_apps };
    tinyos::kernel::vfs::Node g_ram_block = { "ram-block0", false, BlockDeviceText, nullptr, sizeof(BlockDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_framebuffer = { "vga-text-grid", false, FramebufferDeviceText, nullptr, sizeof(FramebufferDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_ui_events = { "ui-event-queue", false, UiEventDeviceText, nullptr, sizeof(UiEventDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_notes = { "notes.txt", false, nullptr, g_notes_buffer, sizeof(NotesInitialText) - 1, sizeof(g_notes_buffer) - 1, true, &g_users };

    tinyos::kernel::vfs::Node* g_nodes[] = {
        &g_root,
        &g_apps,
        &g_system,
        &g_devices,
        &g_users,
        &g_readme,
        &g_shell_info,
        &g_system_info,
        &g_requirements_info,
        &g_runtime_info,
        &g_apps_info,
        &g_tapp_info,
        &g_trust_info,
        &g_tools_info,
        &g_provisioning_info,
        &g_ui_info,
        &g_example_tapp,
        &g_console,
        &g_ram_block,
        &g_framebuffer,
        &g_ui_events,
        &g_notes
    };

    bool g_ready = false;

    bool name_equals_segment(const char* name, const char* segment, size_t length)
    {
        size_t index = 0;
        while (index < length && name[index] != '\0')
        {
            if (name[index] != segment[index])
            {
                return false;
            }

            ++index;
        }

        return index == length && name[index] == '\0';
    }

    tinyos::kernel::vfs::Node* child_by_segment(tinyos::kernel::vfs::Node* parent, const char* segment, size_t length)
    {
        if (parent == nullptr || !parent->directory)
        {
            return nullptr;
        }

        for (size_t index = 0; index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++index)
        {
            auto* node = g_nodes[index];
            if (node->parent == parent && name_equals_segment(node->name, segment, length))
            {
                return node;
            }
        }

        return nullptr;
    }

    tinyos::kernel::vfs::Node* find_mutable(const char* path)
    {
        if (path == nullptr || path[0] == '\0')
        {
            return &g_root;
        }

        auto* current = &g_root;
        const char* cursor = path;
        while (*cursor == '/')
        {
            ++cursor;
        }

        if (*cursor == '\0')
        {
            return current;
        }

        while (*cursor != '\0')
        {
            const char* segment = cursor;
            size_t length = 0;
            while (cursor[length] != '\0' && cursor[length] != '/')
            {
                ++length;
            }

            if (length == 0)
            {
                ++cursor;
                continue;
            }

            current = child_by_segment(current, segment, length);
            if (current == nullptr)
            {
                return nullptr;
            }

            cursor += length;
            while (*cursor == '/')
            {
                ++cursor;
            }
        }

        return current;
    }

    const char* readable_data(const tinyos::kernel::vfs::Node* node)
    {
        if (node == nullptr)
        {
            return nullptr;
        }

        return node->writable_data != nullptr ? node->writable_data : node->readonly_data;
    }
}

namespace tinyos::kernel::vfs::ramfs
{
    void initialize()
    {
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "RAMFS scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    const Node* root()
    {
        return g_ready ? &g_root : nullptr;
    }

    const Node* find(const char* path)
    {
        return g_ready ? find_mutable(path) : nullptr;
    }

    size_t child_count(const Node* node)
    {
        if (!g_ready || node == nullptr || !node->directory)
        {
            return 0;
        }

        size_t count = 0;
        for (size_t index = 0; index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++index)
        {
            if (g_nodes[index]->parent == node)
            {
                ++count;
            }
        }

        return count;
    }

    const Node* child_at(const Node* node, size_t index)
    {
        if (!g_ready || node == nullptr || !node->directory)
        {
            return nullptr;
        }

        size_t current_child = 0;
        for (size_t node_index = 0; node_index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++node_index)
        {
            if (g_nodes[node_index]->parent != node)
            {
                continue;
            }

            if (current_child == index)
            {
                return g_nodes[node_index];
            }

            ++current_child;
        }

        return nullptr;
    }

    bool read_file(const Node* node, const char*& data, size_t& size)
    {
        data = nullptr;
        size = 0;
        if (!g_ready || node == nullptr || node->directory)
        {
            return false;
        }

        data = readable_data(node);
        size = node->size;
        return data != nullptr;
    }

    bool write_file(const char* path, const char* data, size_t size)
    {
        if (!g_ready || data == nullptr)
        {
            return false;
        }

        auto* node = find_mutable(path);
        if (node == nullptr || node->directory || !node->writable || node->writable_data == nullptr || size > node->capacity)
        {
            return false;
        }

        for (size_t index = 0; index < size; ++index)
        {
            node->writable_data[index] = data[index];
        }

        node->writable_data[size] = '\0';
        node->size = size;
        return true;
    }
}
