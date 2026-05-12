#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>

namespace
{
    constexpr size_t MaxRuntimeDirectories = 8;
    constexpr size_t MaxRuntimeFiles = 8;
    constexpr size_t MaxRuntimeNameLength = 48;
    constexpr size_t MaxRuntimeFileBytes = 512;
    constexpr char ReadmeText[] = "TinyOS RAMFS\nUse status, syscheck, riskinfo, pathcheck and helpsearch for terminal-first diagnostics. Use pwd, cd, files, fsmap, show, describe, mkdir, touch, write, copy, move, remove, chmod and fstest for files. Use /receipts for mock install and provisioning receipts. Use tools for the management command manifest. Compatibility aliases are optional.\n";
    constexpr char ShellText[] = "kernel-shell=active\nterminal-dashboard=status\nterminal-self-check=syscheck\npath-diagnostics=pathcheck\ncommand-discovery=helpsearch\nrisk-diagnostics=riskinfo\nfuture-userland-shell=planned\n";
    constexpr char SystemText[] = "name=TinyOS\narch=i686\nstorage=ramfs\nprofile=/system/profile.txt\ndiagnostics=status,syscheck,riskinfo,pathcheck,helpsearch,profileinfo,profilecheck\n";
    constexpr char RequirementsText[] = "arch=i686\nboot=grub-multiboot-iso\nminimum-ram=32MiB\nminimum-ram-probe=make test-minimal-probe\nrecommended-ram=128MiB\ndisplay=vga-text-80x25\ninput=ps2-keyboard\nemulator=qemu-system-i386\n";
    constexpr char RuntimeText[] = "active=native-c-elf32,native-cpp-elf32\nabi=tinyos-native-i686-v0\nplanned=wasm32-sandbox,tiny-bytecode,tiny-script\nsecurity=capability-gated\nselfhost=planned-native-toolchain\n";
    constexpr char AppsText[] = "ready=system-shell\nplanned=example-system-tool,desktop-shell,web-gui-host,selfhost-toolchain,bytecode-service\npolicy=runtime-capability-subset\nlauncher=profile-check-only\nsecurity=least-privilege-before-launch\ncommands=appinfo,launchinfo,launchcheck\n";
    constexpr char TappText[] = "extension=.tapp\nformat=tinyos-tapp-v0\nsignature=detached-rsa-sha256\nreceipt=.sig.receipt\nready=system-shell.tapp\nvalid=example-system-tool.tapp\nplanned=desktop-shell.tapp,web-gui-host.tapp,selfhost-toolchain.tapp\ncommands=tappinfo,tapps,tapp,tappcheck,tappverify\nhost-tools=keygen-app,trust-app,sign-app,verify-app\npolicy=signed-manifest-required,payload-hash-required,capability-subset-required\nverifier=install-gate-contract\n";
    constexpr char TrustText[] = "store=tapp-trust-v0\nready=tinyos-dev-app-signing\nplanned=tinyos-release-app-root,tinyos-image-signing-root,tinyos-recovery-root\nrevoked=tinyos-revoked-test-key\nalgorithm=rsa-sha256\ndev-key=build/keys/tapp-dev-public.pem\nhost-tool=scripts/tinyos-image.sh trust-app\ncommands=trustinfo,trust\npolicy=development-key-not-for-release,revoked-keys-never-match\n";
    constexpr char ToolsText[] = "ready=help,helpui,helpsearch,helplist,fileui,status,syscheck,riskinfo,profileinfo,profilecheck,tools,toolinfo,tool,pwd,cd,files,fsmap,show,describe,pathcheck,mkdir,touch,chmod,write,copy,move,remove,fstest,devices,device,blockinfo,storageinfo,meminfo,frameinfo,heapinfo,paginginfo,addrspaceinfo,runtimeinfo,appinfo,launchinfo,launchcheck,tappinfo,tapps,tapp,tappcheck,tappverify,trustinfo,trust,imageinfo,provisioninfo,deployinfo,installinfo,installcheck,install,securityinfo,integritycheck,renderinfo,terminalinfo,widgetinfo,uieventinfo,schedinfo,taskinfo,timerinfo,uptime,reboot\nplanned=mount,ps,kill,service,useradd,hostname,netconfig,passwd,whoami,id,package,tappinstall,tappremove,imagebuild,imagesign,imageencrypt,keygen,provisionui,provisioninit,provisionconfig,provisionvariant,provisionapi,provisionresources,remoteaccess,terminaltheme,videomode,deploy,provision,rollback,netinfo\npolicy=high-risk-tools-must-be-explicit\n";
    constexpr char ProvisioningText[] = "pipeline=project-workspace,provision-config,device-variants,project-api,resource-budget,app-bundle,app-signature,system-profile,encryption-default,image-manifest,image-build,image-sign,image-encrypt,remote-folder-access,deploy-check,deploy-ssh,target-verify,rollback-slot\nhost-tool=scripts/tinyos-image.sh\nready-contracts=app-bundle,app-signature,system-profile,encryption-default,image-manifest,deploy-check,deploy-receipt\nplanned-host-tools=provisioninit,provisionconfig,provisionvariant,provisionresources,imagebuild,imagesign,imageencrypt,keygen,remoteaccess,deploy\nplanned-kernel=provisionapi,provisionui,terminaltheme,videomode,target-verify,rollback-slot\ntransport=ssh-sftp-now,tinylink-later\npolicy=isolated-workspace,sign-before-deploy,encrypt-by-default,deploy-check-before-transport,remote-access-opt-in,rollback-before-activate\n";
    constexpr char InstallText[] = "state=ready-contract,ready-mock\nmedia=iso-current,disk-install-planned\nprofile=examples/install.profile\nhost-tools=install-plan,check-install-profile\ncommands=installinfo,installcheck,install\nreceipt=/receipts/install.receipt\ninputs=device.name,network.mode,user.name,credential.bootstrap,admin.mode\nmock-policy=ramfs-receipt-only,no-disk-writes\ncredential-policy=prompt-only,password-hashing-required,no-plaintext-secrets\nadmin-policy=shared-bootstrap-development-only,separate-secret-or-key-release\nprovisioning=available-after-first-boot\n";
    constexpr char ProfileText[] = "tinyos.profile.version=0\nstate=ramfs-default\nsource=/system/profile.txt\ndevice.name=tinyos-dev-vm\ndevice.variant=qemu-i686-terminal\nboot.target=i686-pc-qemu\nnetwork.mode=disabled\nuser.bootstrap=developer\ncredential.bootstrap=prompt\nadmin.mode=same-bootstrap-secret\nsecurity.password_hashing=required\nsecurity.plaintext_secrets=forbidden\nprovisioning.encryption=required\nprovisioning.remote_access=disabled\nstorage.persistence=ramfs-only\ninstall.state=mock\n";
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
    tinyos::kernel::vfs::Node g_receipts = { "receipts", true, nullptr, nullptr, 0, 0, false, &g_root };
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
    tinyos::kernel::vfs::Node g_install_info = { "install.txt", false, InstallText, nullptr, sizeof(InstallText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_profile_info = { "profile.txt", false, ProfileText, nullptr, sizeof(ProfileText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_ui_info = { "ui.txt", false, UiText, nullptr, sizeof(UiText) - 1, 0, false, &g_system };
    tinyos::kernel::vfs::Node g_console = { "console", false, ConsoleText, nullptr, sizeof(ConsoleText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_example_tapp = { "example-system-tool.tapp", false, ExampleTappText, nullptr, sizeof(ExampleTappText) - 1, 0, false, &g_apps };
    tinyos::kernel::vfs::Node g_ram_block = { "ram-block0", false, BlockDeviceText, nullptr, sizeof(BlockDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_framebuffer = { "vga-text-grid", false, FramebufferDeviceText, nullptr, sizeof(FramebufferDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_ui_events = { "ui-event-queue", false, UiEventDeviceText, nullptr, sizeof(UiEventDeviceText) - 1, 0, false, &g_devices };
    tinyos::kernel::vfs::Node g_notes = { "notes.txt", false, nullptr, g_notes_buffer, sizeof(NotesInitialText) - 1, sizeof(g_notes_buffer) - 1, true, &g_users };
    tinyos::kernel::vfs::Node g_runtime_directories[MaxRuntimeDirectories];
    char g_runtime_directory_names[MaxRuntimeDirectories][MaxRuntimeNameLength + 1];
    bool g_runtime_directory_used[MaxRuntimeDirectories];
    uint16_t g_runtime_directory_modes[MaxRuntimeDirectories];
    tinyos::kernel::vfs::Node g_runtime_files[MaxRuntimeFiles];
    char g_runtime_file_names[MaxRuntimeFiles][MaxRuntimeNameLength + 1];
    char g_runtime_file_data[MaxRuntimeFiles][MaxRuntimeFileBytes + 1];
    bool g_runtime_file_used[MaxRuntimeFiles];
    uint16_t g_runtime_file_modes[MaxRuntimeFiles];

    tinyos::kernel::vfs::Node* g_nodes[] = {
        &g_root,
        &g_apps,
        &g_system,
        &g_devices,
        &g_users,
        &g_receipts,
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
        &g_install_info,
        &g_profile_info,
        &g_ui_info,
        &g_example_tapp,
        &g_console,
        &g_ram_block,
        &g_framebuffer,
        &g_ui_events,
        &g_notes
    };
    uint16_t g_static_node_modes[sizeof(g_nodes) / sizeof(g_nodes[0])];

    bool g_ready = false;

    uint16_t default_mode_for(const tinyos::kernel::vfs::Node* node)
    {
        if (node == nullptr)
        {
            return 0;
        }

        if (node->directory)
        {
            return 0755;
        }

        return node->writable ? 0644 : 0444;
    }

    size_t static_node_count()
    {
        return sizeof(g_nodes) / sizeof(g_nodes[0]);
    }

    size_t runtime_node_count()
    {
        size_t count = 0;
        for (size_t index = 0; index < MaxRuntimeDirectories; ++index)
        {
            if (g_runtime_directory_used[index])
            {
                ++count;
            }
        }

        for (size_t index = 0; index < MaxRuntimeFiles; ++index)
        {
            if (g_runtime_file_used[index])
            {
                ++count;
            }
        }

        return count;
    }

    tinyos::kernel::vfs::Node* node_at(size_t index)
    {
        if (index < static_node_count())
        {
            return g_nodes[index];
        }

        size_t runtime_index = index - static_node_count();
        for (size_t slot = 0; slot < MaxRuntimeDirectories; ++slot)
        {
            if (!g_runtime_directory_used[slot])
            {
                continue;
            }

            if (runtime_index == 0)
            {
                return &g_runtime_directories[slot];
            }

            --runtime_index;
        }

        for (size_t slot = 0; slot < MaxRuntimeFiles; ++slot)
        {
            if (!g_runtime_file_used[slot])
            {
                continue;
            }

            if (runtime_index == 0)
            {
                return &g_runtime_files[slot];
            }

            --runtime_index;
        }

        return nullptr;
    }

    size_t node_count()
    {
        return static_node_count() + runtime_node_count();
    }

    bool static_node_index(const tinyos::kernel::vfs::Node* node, size_t& result)
    {
        for (size_t index = 0; index < static_node_count(); ++index)
        {
            if (g_nodes[index] == node)
            {
                result = index;
                return true;
            }
        }

        return false;
    }

    bool runtime_directory_index(const tinyos::kernel::vfs::Node* node, size_t& result)
    {
        for (size_t index = 0; index < MaxRuntimeDirectories; ++index)
        {
            if (g_runtime_directory_used[index] && &g_runtime_directories[index] == node)
            {
                result = index;
                return true;
            }
        }

        return false;
    }

    bool runtime_file_index(const tinyos::kernel::vfs::Node* node, size_t& result)
    {
        for (size_t index = 0; index < MaxRuntimeFiles; ++index)
        {
            if (g_runtime_file_used[index] && &g_runtime_files[index] == node)
            {
                result = index;
                return true;
            }
        }

        return false;
    }

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

        for (size_t index = 0; index < node_count(); ++index)
        {
            auto* node = node_at(index);
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

    bool directory_allows_changes(const tinyos::kernel::vfs::Node* node)
    {
        return node != nullptr && node->directory && (tinyos::kernel::vfs::ramfs::access_mode(node) & 0300) == 0300;
    }

    bool split_parent_leaf(const char* path, tinyos::kernel::vfs::Node*& parent, const char*& leaf, size_t& leaf_length)
    {
        parent = nullptr;
        leaf = nullptr;
        leaf_length = 0;
        if (path == nullptr || path[0] != '/' || path[1] == '\0')
        {
            return false;
        }

        parent = &g_root;
        const char* cursor = path;
        while (*cursor == '/')
        {
            ++cursor;
        }

        while (*cursor != '\0')
        {
            const char* segment = cursor;
            size_t length = 0;
            while (cursor[length] != '\0' && cursor[length] != '/')
            {
                ++length;
            }

            cursor += length;
            while (*cursor == '/')
            {
                ++cursor;
            }

            if (*cursor == '\0')
            {
                leaf = segment;
                leaf_length = length;
                return leaf_length != 0 && leaf_length <= MaxRuntimeNameLength;
            }

            parent = child_by_segment(parent, segment, length);
            if (parent == nullptr || !parent->directory)
            {
                return false;
            }
        }

        return false;
    }

    void copy_name(char* destination, const char* source, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            destination[index] = source[index];
        }

        destination[length] = '\0';
    }

    bool create_runtime_file(const char* path, const char* data, size_t size)
    {
        if (!g_ready || path == nullptr || path[0] != '/' || find_mutable(path) != nullptr || size > MaxRuntimeFileBytes)
        {
            return false;
        }

        tinyos::kernel::vfs::Node* parent = nullptr;
        const char* leaf = nullptr;
        size_t leaf_length = 0;
        if (!split_parent_leaf(path, parent, leaf, leaf_length) || child_by_segment(parent, leaf, leaf_length) != nullptr || !directory_allows_changes(parent))
        {
            return false;
        }

        for (size_t slot = 0; slot < MaxRuntimeFiles; ++slot)
        {
            if (g_runtime_file_used[slot])
            {
                continue;
            }

            copy_name(g_runtime_file_names[slot], leaf, leaf_length);
            for (size_t index = 0; index < size; ++index)
            {
                g_runtime_file_data[slot][index] = data != nullptr ? data[index] : '\0';
            }
            g_runtime_file_data[slot][size] = '\0';

            auto* node = &g_runtime_files[slot];
            node->name = g_runtime_file_names[slot];
            node->directory = false;
            node->readonly_data = nullptr;
            node->writable_data = g_runtime_file_data[slot];
            node->size = size;
            node->capacity = MaxRuntimeFileBytes;
            node->writable = true;
            node->parent = parent;
            g_runtime_file_modes[slot] = 0644;
            g_runtime_file_used[slot] = true;
            return true;
        }

        return false;
    }

    bool is_descendant_of(const tinyos::kernel::vfs::Node* node, const tinyos::kernel::vfs::Node* possible_ancestor)
    {
        const auto* current = node;
        while (current != nullptr)
        {
            if (current == possible_ancestor)
            {
                return true;
            }

            current = current->parent;
        }

        return false;
    }
}

namespace tinyos::kernel::vfs::ramfs
{
    void initialize()
    {
        for (size_t index = 0; index < static_node_count(); ++index)
        {
            g_static_node_modes[index] = default_mode_for(g_nodes[index]);
        }

        for (size_t index = 0; index < MaxRuntimeDirectories; ++index)
        {
            g_runtime_directory_used[index] = false;
            g_runtime_directory_names[index][0] = '\0';
            g_runtime_directory_modes[index] = 0755;
        }

        for (size_t index = 0; index < MaxRuntimeFiles; ++index)
        {
            g_runtime_file_used[index] = false;
            g_runtime_file_names[index][0] = '\0';
            g_runtime_file_data[index][0] = '\0';
            g_runtime_file_modes[index] = 0644;
        }

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
        for (size_t index = 0; index < node_count(); ++index)
        {
            const auto* current = node_at(index);
            if (current != nullptr && current->parent == node)
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
        for (size_t node_index = 0; node_index < node_count(); ++node_index)
        {
            const auto* current = node_at(node_index);
            if (current == nullptr || current->parent != node)
            {
                continue;
            }

            if (current_child == index)
            {
                return current;
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

        if ((tinyos::kernel::vfs::ramfs::access_mode(node) & 0400) == 0)
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
        if (node == nullptr || node->directory || !node->writable || (tinyos::kernel::vfs::ramfs::access_mode(node) & 0200) == 0 || node->writable_data == nullptr || size > node->capacity)
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

    bool create_directory(const char* path)
    {
        if (!g_ready || path == nullptr || path[0] != '/' || find_mutable(path) != nullptr)
        {
            return false;
        }

        tinyos::kernel::vfs::Node* parent = nullptr;
        const char* leaf = nullptr;
        size_t leaf_length = 0;
        if (!split_parent_leaf(path, parent, leaf, leaf_length) || child_by_segment(parent, leaf, leaf_length) != nullptr || !directory_allows_changes(parent))
        {
            return false;
        }

        for (size_t slot = 0; slot < MaxRuntimeDirectories; ++slot)
        {
            if (g_runtime_directory_used[slot])
            {
                continue;
            }

            copy_name(g_runtime_directory_names[slot], leaf, leaf_length);

            auto* node = &g_runtime_directories[slot];
            node->name = g_runtime_directory_names[slot];
            node->directory = true;
            node->readonly_data = nullptr;
            node->writable_data = nullptr;
            node->size = 0;
            node->capacity = 0;
            node->writable = true;
            node->parent = parent;
            g_runtime_directory_modes[slot] = 0755;
            g_runtime_directory_used[slot] = true;
            return true;
        }

        return false;
    }

    bool create_file(const char* path)
    {
        return create_runtime_file(path, nullptr, 0);
    }

    bool remove(const char* path)
    {
        if (!g_ready || path == nullptr || path[0] != '/')
        {
            return false;
        }

        auto* node = find_mutable(path);
        if (node == nullptr || node == &g_root || !directory_allows_changes(node->parent))
        {
            return false;
        }

        size_t index = 0;
        if (runtime_file_index(node, index))
        {
            for (size_t offset = 0; offset <= MaxRuntimeFileBytes; ++offset)
            {
                g_runtime_file_data[index][offset] = '\0';
            }
            g_runtime_file_names[index][0] = '\0';
            g_runtime_file_modes[index] = 0644;
            g_runtime_file_used[index] = false;
            return true;
        }

        if (runtime_directory_index(node, index))
        {
            if (tinyos::kernel::vfs::ramfs::child_count(node) != 0)
            {
                return false;
            }

            g_runtime_directory_names[index][0] = '\0';
            g_runtime_directory_modes[index] = 0755;
            g_runtime_directory_used[index] = false;
            return true;
        }

        return false;
    }

    bool copy_file(const char* source_path, const char* destination_path)
    {
        const auto* source = find_mutable(source_path);
        const char* data = nullptr;
        size_t size = 0;
        if (!tinyos::kernel::vfs::ramfs::read_file(source, data, size) || find_mutable(destination_path) != nullptr)
        {
            return false;
        }

        return create_runtime_file(destination_path, data, size);
    }

    bool move(const char* source_path, const char* destination_path)
    {
        if (!g_ready || source_path == nullptr || destination_path == nullptr || find_mutable(destination_path) != nullptr)
        {
            return false;
        }

        auto* node = find_mutable(source_path);
        if (node == nullptr || node == &g_root || !directory_allows_changes(node->parent))
        {
            return false;
        }

        tinyos::kernel::vfs::Node* new_parent = nullptr;
        const char* leaf = nullptr;
        size_t leaf_length = 0;
        if (!split_parent_leaf(destination_path, new_parent, leaf, leaf_length) || child_by_segment(new_parent, leaf, leaf_length) != nullptr || !directory_allows_changes(new_parent))
        {
            return false;
        }

        if (node->directory && is_descendant_of(new_parent, node))
        {
            return false;
        }

        size_t index = 0;
        if (runtime_file_index(node, index))
        {
            copy_name(g_runtime_file_names[index], leaf, leaf_length);
            g_runtime_files[index].parent = new_parent;
            return true;
        }

        if (runtime_directory_index(node, index))
        {
            copy_name(g_runtime_directory_names[index], leaf, leaf_length);
            g_runtime_directories[index].parent = new_parent;
            return true;
        }

        return false;
    }

    uint16_t access_mode(const Node* node)
    {
        size_t index = 0;
        if (static_node_index(node, index))
        {
            return g_static_node_modes[index];
        }

        if (runtime_directory_index(node, index))
        {
            return g_runtime_directory_modes[index];
        }

        if (runtime_file_index(node, index))
        {
            return g_runtime_file_modes[index];
        }

        return 0;
    }

    bool set_access_mode(const char* path, uint16_t mode)
    {
        if (!g_ready || mode > 0777)
        {
            return false;
        }

        auto* node = find_mutable(path);
        size_t index = 0;
        if (static_node_index(node, index))
        {
            g_static_node_modes[index] = mode;
            return true;
        }

        if (runtime_directory_index(node, index))
        {
            g_runtime_directory_modes[index] = mode;
            return true;
        }

        if (runtime_file_index(node, index))
        {
            g_runtime_file_modes[index] = mode;
            return true;
        }

        return false;
    }
}
