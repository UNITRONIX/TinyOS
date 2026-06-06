#include <stddef.h>
#include <stdint.h>

#include <tinyos/arch/context.hpp>
#include <tinyos/arch/hal.hpp>
#include <tinyos/config.hpp>
#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/drivers/input.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/drivers/pic.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/api/system_api.hpp>
#include <tinyos/kernel/admin/tools.hpp>
#include <tinyos/kernel/app/launcher.hpp>
#include <tinyos/kernel/app/manifest.hpp>
#include <tinyos/kernel/app/package.hpp>
#include <tinyos/kernel/app/package_verifier.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/device/block.hpp>
#include <tinyos/kernel/device/framebuffer.hpp>
#include <tinyos/kernel/device/registry.hpp>
#include <tinyos/kernel/elf/loader.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/interrupts.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/address_space.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/heap.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>
#include <tinyos/kernel/platform/pc.hpp>
#include <tinyos/kernel/platform/requirements.hpp>
#include <tinyos/kernel/provision/image.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/security/integrity.hpp>
#include <tinyos/kernel/security/trust.hpp>
#include <tinyos/kernel/syscall/syscall.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/kernel/user/transition.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>
#include <tinyos/kernel/vfs/mount.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/shell/shell.hpp>
#if !defined(TINYOS_TERMINAL_ONLY)
#include <tinyos/ui/cursor.hpp>
#include <tinyos/ui/desktop.hpp>
#include <tinyos/ui/graphical_desktop.hpp>
#endif
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>
#if !defined(TINYOS_TERMINAL_ONLY)
#include <tinyos/ui/window_manager.hpp>
#endif
#include <tinyos/ui/widgets.hpp>

namespace
{
    constexpr size_t MaxInputLength = 128;
    constexpr size_t MaxPathLength = 96;
    constexpr size_t TextEditorBufferBytes = 512;
    constexpr const char* SystemProfilePath = "/system/profile.txt";
    constexpr const char* InstallReceiptPath = "/receipts/install.receipt";
    constexpr char InstallReceiptText[] =
        "tinyos.install.receipt.version=0\n"
        "state=mock-installed\n"
        "profile=examples/install.profile\n"
        "media=iso-current\n"
        "target=i686-pc-qemu\n"
        "disk.write=disabled\n"
        "receipt.storage=ramfs-runtime\n"
        "device.name=tinyos-dev-vm\n"
        "network.mode=disabled\n"
        "user.name=developer\n"
        "credential.bootstrap=prompt\n"
        "admin.mode=same-bootstrap-secret\n"
        "security.password_hashing=required\n"
        "security.plaintext_secrets=forbidden\n"
        "provisioning.encryption=required\n"
        "provisioning.remote_access=disabled\n"
        "next=persistent-disk-install-planned\n";
    char g_current_directory[MaxPathLength] = "/";
    char g_textedit_buffer[TextEditorBufferBytes + 1];
    char g_textedit_line[MaxInputLength];

    void wait_for_key();
    void write_check_result(const char* label, bool passed);
    void fileui_show_selected(const char* current_path, size_t selected);

    void debug_shell_checkpoint(const char* stage)
    {
#if defined(TINYOS_DEBUG_BOOT)
        tinyos::drivers::serial::write("[debug-boot] ");
        tinyos::drivers::serial::write_line(stage);
#else
        (void)stage;
#endif
    }

    void write_uint64(uint64_t value)
    {
        char buffer[21];
        buffer[20] = '\0';

        int index = 19;
        do
        {
            buffer[index] = static_cast<char>('0' + (value % 10));
            value /= 10;
            --index;
        } while (value != 0 && index >= 0);

        tinyos::drivers::vga::write(&buffer[index + 1]);
    }

    void write_octal_mode(uint16_t mode)
    {
        tinyos::drivers::vga::put_char(static_cast<char>('0' + ((mode >> 6) & 7)));
        tinyos::drivers::vga::put_char(static_cast<char>('0' + ((mode >> 3) & 7)));
        tinyos::drivers::vga::put_char(static_cast<char>('0' + (mode & 7)));
    }

    void write_yes_no(bool value)
    {
        tinyos::drivers::vga::write_line(value ? "yes" : "no");
    }

    bool parse_octal_mode(const char* text, uint16_t& mode)
    {
        text = tinyos::core::string::skip_spaces(text);
        if (text == nullptr || text[0] == '\0')
        {
            return false;
        }

        uint16_t value = 0;
        size_t digits = 0;
        while (text[digits] != '\0' && text[digits] != ' ')
        {
            if (text[digits] < '0' || text[digits] > '7' || digits >= 4)
            {
                return false;
            }

            value = static_cast<uint16_t>((value << 3) + static_cast<uint16_t>(text[digits] - '0'));
            ++digits;
        }

        if (digits == 0 || value > 0777)
        {
            return false;
        }

        mode = value;
        return true;
    }

    bool copy_argument(const char* text, char* destination, size_t destination_size, const char*& rest)
    {
        text = tinyos::core::string::skip_spaces(text);
        rest = text;
        if (destination_size == 0 || text[0] == '\0')
        {
            return false;
        }

        size_t index = 0;
        while (text[index] != '\0' && text[index] != ' ')
        {
            if (index + 1 >= destination_size)
            {
                destination[0] = '\0';
                return false;
            }

            destination[index] = text[index];
            ++index;
        }

        destination[index] = '\0';
        rest = tinyos::core::string::skip_spaces(text + index);
        return true;
    }

    bool copy_path_string(char* destination, size_t destination_size, const char* source)
    {
        if (destination == nullptr || destination_size == 0 || source == nullptr)
        {
            return false;
        }

        size_t index = 0;
        while (source[index] != '\0')
        {
            if (index + 1 >= destination_size)
            {
                destination[0] = '\0';
                return false;
            }

            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
        return true;
    }

    bool pop_path_segment(char* path)
    {
        if (path == nullptr || path[0] != '/')
        {
            return false;
        }

        if (path[1] == '\0')
        {
            return true;
        }

        size_t length = tinyos::core::string::length(path);
        while (length > 1 && path[length - 1] == '/')
        {
            path[length - 1] = '\0';
            --length;
        }

        size_t slash = length - 1;
        while (slash > 0 && path[slash] != '/')
        {
            --slash;
        }

        if (slash == 0)
        {
            path[1] = '\0';
            return true;
        }

        path[slash] = '\0';
        return true;
    }

    bool append_path_segment(char* path, size_t path_size, const char* segment, size_t segment_length)
    {
        if (path == nullptr || path_size == 0 || segment == nullptr)
        {
            return false;
        }

        if (segment_length == 0 || (segment_length == 1 && segment[0] == '.'))
        {
            return true;
        }

        if (segment_length == 2 && segment[0] == '.' && segment[1] == '.')
        {
            return pop_path_segment(path);
        }

        size_t path_length = tinyos::core::string::length(path);
        if (path_length == 0 || path[0] != '/')
        {
            return false;
        }

        const bool need_separator = !(path_length == 1 && path[0] == '/');
        if (path_length + (need_separator ? 1 : 0) + segment_length >= path_size)
        {
            return false;
        }

        if (need_separator)
        {
            path[path_length] = '/';
            ++path_length;
        }

        for (size_t index = 0; index < segment_length; ++index)
        {
            path[path_length + index] = segment[index];
        }

        path[path_length + segment_length] = '\0';
        return true;
    }

    bool resolve_path_from(const char* base, const char* input, char* output, size_t output_size)
    {
        input = tinyos::core::string::skip_spaces(input);
        if (base == nullptr || input == nullptr || output == nullptr || output_size == 0)
        {
            return false;
        }

        if (input[0] == '/')
        {
            if (!copy_path_string(output, output_size, "/"))
            {
                return false;
            }
        }
        else if (!copy_path_string(output, output_size, base))
        {
            return false;
        }

        const char* cursor = input;
        while (*cursor == '/')
        {
            ++cursor;
        }

        while (*cursor != '\0')
        {
            const char* segment = cursor;
            size_t segment_length = 0;
            while (cursor[segment_length] != '\0' && cursor[segment_length] != '/')
            {
                ++segment_length;
            }

            if (!append_path_segment(output, output_size, segment, segment_length))
            {
                return false;
            }

            cursor += segment_length;
            while (*cursor == '/')
            {
                ++cursor;
            }
        }

        return tinyos::kernel::vfs::validate_path(output);
    }

    bool resolve_shell_path(const char* input, char* output, size_t output_size)
    {
        return resolve_path_from(g_current_directory, input, output, output_size);
    }

    bool build_child_path(const char* parent, const char* name, char* output, size_t output_size)
    {
        return resolve_path_from(parent, name, output, output_size);
    }

    void write_buffer(const char* data, size_t size)
    {
        for (size_t index = 0; index < size; ++index)
        {
            tinyos::drivers::vga::put_char(data[index]);
        }
    }

    void print_node_entry(const tinyos::kernel::vfs::Node* node)
    {
        tinyos::drivers::vga::write(node != nullptr && node->directory ? "[dir]  " : "[file] ");
        tinyos::drivers::vga::write(node != nullptr && node->name != nullptr ? node->name : "invalid");
        if (node != nullptr && !node->directory)
        {
            tinyos::drivers::vga::write(" (");
            write_uint64(node->size);
            tinyos::drivers::vga::write(node->writable ? " bytes, writable" : " bytes");
            tinyos::drivers::vga::write(")");
        }

        tinyos::drivers::vga::put_char('\n');
    }

    void list_path(const char* path)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr)
        {
            tinyos::drivers::vga::write_line("Path not found.");
            return;
        }

        if (!node->directory)
        {
            print_node_entry(node);
            return;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            tinyos::drivers::vga::write_line("Directory not readable.");
            return;
        }

        for (size_t index = 0; index < tinyos::kernel::vfs::child_count(node); ++index)
        {
            print_node_entry(tinyos::kernel::vfs::child_at(node, index));
        }
    }

    void print_tree(const tinyos::kernel::vfs::Node* node, size_t depth)
    {
        if (node == nullptr)
        {
            return;
        }

        for (size_t index = 0; index < depth; ++index)
        {
            tinyos::drivers::vga::write("  ");
        }

        tinyos::drivers::vga::write(node->name != nullptr ? node->name : "invalid");
        if (node->directory)
        {
            tinyos::drivers::vga::write("/");
        }

        tinyos::drivers::vga::put_char('\n');
        if (!node->directory)
        {
            return;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            for (size_t index = 0; index <= depth; ++index)
            {
                tinyos::drivers::vga::write("  ");
            }
            tinyos::drivers::vga::write_line("<permission denied>");
            return;
        }

        for (size_t index = 0; index < tinyos::kernel::vfs::child_count(node); ++index)
        {
            print_tree(tinyos::kernel::vfs::child_at(node, index), depth + 1);
        }
    }

    void show_file(const char* path)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const char* data = nullptr;
        size_t size = 0;
        if (!tinyos::kernel::vfs::read_file(node, data, size))
        {
            tinyos::drivers::vga::write_line("File not found or not readable.");
            return;
        }

        write_buffer(data, size);
        if (size == 0 || data[size - 1] != '\n')
        {
            tinyos::drivers::vga::put_char('\n');
        }
    }

    void show_file_info(const char* path)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr)
        {
            tinyos::drivers::vga::write_line("Path not found.");
            return;
        }

        tinyos::drivers::vga::write("Name     : ");
        tinyos::drivers::vga::write_line(node->name != nullptr ? node->name : "invalid");
        tinyos::drivers::vga::write("Type     : ");
        tinyos::drivers::vga::write_line(node->directory ? "directory" : "file");
        tinyos::drivers::vga::write("Size     : ");
        write_uint64(node->size);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Writable : ");
        tinyos::drivers::vga::write_line(node->writable ? "yes" : "no");
        tinyos::drivers::vga::write("Mode     : ");
        write_octal_mode(tinyos::kernel::vfs::access_mode(node));
        tinyos::drivers::vga::put_char('\n');
        if (node->directory)
        {
            tinyos::drivers::vga::write("Children : ");
            if (tinyos::kernel::vfs::can_list_directory(node))
            {
                write_uint64(tinyos::kernel::vfs::child_count(node));
                tinyos::drivers::vga::put_char('\n');
            }
            else
            {
                tinyos::drivers::vga::write_line("permission denied");
            }
        }
        else
        {
            tinyos::drivers::vga::write("Capacity : ");
            write_uint64(node->capacity);
            tinyos::drivers::vga::put_char('\n');
        }
    }

    void edit_file(const char* path, const char* text)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr || node->directory)
        {
            tinyos::drivers::vga::write_line("Editable file not found.");
            return;
        }

        if (!node->writable)
        {
            tinyos::drivers::vga::write_line("File is read-only.");
            return;
        }

        const size_t size = tinyos::core::string::length(text);
        if (size > node->capacity)
        {
            tinyos::drivers::vga::write_line("Text is too large for this RAMFS file.");
            return;
        }

        if (!tinyos::kernel::vfs::write_file(path, text, size))
        {
            tinyos::drivers::vga::write_line("Edit failed.");
            return;
        }

        tinyos::drivers::vga::write_line("File updated.");
    }

    bool buffer_equals(const char* data, size_t size, const char* expected)
    {
        if (data == nullptr || expected == nullptr)
        {
            return false;
        }

        const size_t expected_size = tinyos::core::string::length(expected);
        if (size != expected_size)
        {
            return false;
        }

        for (size_t index = 0; index < size; ++index)
        {
            if (data[index] != expected[index])
            {
                return false;
            }
        }

        return true;
    }

    void record_self_test_result(const char* name, bool passed, size_t& passed_count, size_t& failed_count)
    {
        tinyos::drivers::vga::write(passed ? "[ok]   " : "[fail] ");
        tinyos::drivers::vga::write_line(name);
        if (passed)
        {
            ++passed_count;
        }
        else
        {
            ++failed_count;
        }
    }

    bool file_contains(const char* path, const char* expected)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const char* data = nullptr;
        size_t size = 0;
        return tinyos::kernel::vfs::read_file(node, data, size) && buffer_equals(data, size, expected);
    }

    bool buffer_contains_fragment(const char* data, size_t size, const char* expected)
    {
        if (data == nullptr || expected == nullptr)
        {
            return false;
        }

        const size_t expected_size = tinyos::core::string::length(expected);
        if (expected_size == 0 || expected_size > size)
        {
            return false;
        }

        for (size_t offset = 0; offset + expected_size <= size; ++offset)
        {
            size_t matched = 0;
            while (matched < expected_size && data[offset + matched] == expected[matched])
            {
                ++matched;
            }

            if (matched == expected_size)
            {
                return true;
            }
        }

        return false;
    }

    bool file_contains_fragment(const char* path, const char* expected)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const char* data = nullptr;
        size_t size = 0;
        return tinyos::kernel::vfs::read_file(node, data, size) && buffer_contains_fragment(data, size, expected);
    }

    bool profile_has(const char* expected)
    {
        return file_contains_fragment(SystemProfilePath, expected);
    }

    bool system_profile_contract_passes()
    {
        const auto* profile = tinyos::kernel::vfs::find(SystemProfilePath);
        return profile != nullptr && !profile->directory &&
            tinyos::kernel::vfs::validate_path(SystemProfilePath) &&
            profile_has("tinyos.profile.version=0\n") &&
            profile_has("state=ramfs-default\n") &&
            profile_has("device.variant=qemu-i686-terminal\n") &&
            profile_has("network.mode=disabled\n") &&
            profile_has("credential.bootstrap=prompt\n") &&
            profile_has("security.password_hashing=required\n") &&
            profile_has("security.plaintext_secrets=forbidden\n") &&
            profile_has("provisioning.encryption=required\n") &&
            profile_has("provisioning.remote_access=disabled\n") &&
            profile_has("storage.persistence=ramfs-only\n") &&
            profile_has("install.state=mock\n");
    }

    void print_profile_info()
    {
        tinyos::drivers::vga::write_line("TinyOS active system profile:");
        tinyos::drivers::vga::write("Path     : ");
        tinyos::drivers::vga::write_line(SystemProfilePath);
        tinyos::drivers::vga::write("Valid    : ");
        write_yes_no(system_profile_contract_passes());
        tinyos::drivers::vga::write_line("Metadata:");
        show_file(SystemProfilePath);
    }

    void print_profile_check()
    {
        const auto* profile = tinyos::kernel::vfs::find(SystemProfilePath);
        tinyos::drivers::vga::write_line("TinyOS system profile check:");
        write_check_result("profile file exists", profile != nullptr && !profile->directory);
        write_check_result("profile path validates", tinyos::kernel::vfs::validate_path(SystemProfilePath));
        write_check_result("profile version contract", profile_has("tinyos.profile.version=0\n"));
        write_check_result("RAMFS default state", profile_has("state=ramfs-default\n"));
        write_check_result("QEMU terminal variant", profile_has("device.variant=qemu-i686-terminal\n"));
        write_check_result("network disabled by default", profile_has("network.mode=disabled\n"));
        write_check_result("credential prompt policy", profile_has("credential.bootstrap=prompt\n"));
        write_check_result("password hashing required", profile_has("security.password_hashing=required\n"));
        write_check_result("plaintext secrets forbidden", profile_has("security.plaintext_secrets=forbidden\n"));
        write_check_result("provisioning encryption required", profile_has("provisioning.encryption=required\n"));
        write_check_result("remote access disabled", profile_has("provisioning.remote_access=disabled\n"));
        write_check_result("RAMFS-only persistence", profile_has("storage.persistence=ramfs-only\n"));
        write_check_result("install state is mock", profile_has("install.state=mock\n"));
        tinyos::drivers::vga::write_line(system_profile_contract_passes() ? "System profile check passed." : "System profile check failed.");
    }

    bool install_receipt_current()
    {
        return file_contains(InstallReceiptPath, InstallReceiptText);
    }

    bool installer_mock_preflight_passes()
    {
        const auto* install_info = tinyos::kernel::vfs::find("/system/install.txt");
        const auto* receipts = tinyos::kernel::vfs::find("/receipts");
        return install_info != nullptr && !install_info->directory &&
            receipts != nullptr && receipts->directory &&
            tinyos::kernel::vfs::can_modify_directory(receipts) &&
            tinyos::kernel::vfs::validate_path(InstallReceiptPath);
    }

    void write_check_result(const char* label, bool passed)
    {
        tinyos::drivers::vga::write(passed ? "[ok]   " : "[fail] ");
        tinyos::drivers::vga::write_line(label);
    }

    bool write_install_receipt()
    {
        if (!installer_mock_preflight_passes())
        {
            return false;
        }

        if (tinyos::kernel::vfs::find(InstallReceiptPath) == nullptr && !tinyos::kernel::vfs::create_file(InstallReceiptPath))
        {
            return false;
        }

        return tinyos::kernel::vfs::write_file(InstallReceiptPath, InstallReceiptText, sizeof(InstallReceiptText) - 1);
    }

    void print_install_check()
    {
        const auto* install_info = tinyos::kernel::vfs::find("/system/install.txt");
        const auto* receipts = tinyos::kernel::vfs::find("/receipts");
        tinyos::drivers::vga::write_line("TinyOS installer mock check:");
        write_check_result("/system/install.txt readable contract", install_info != nullptr && !install_info->directory);
        write_check_result("/receipts runtime directory", receipts != nullptr && receipts->directory);
        write_check_result("/receipts accepts RAMFS writes", tinyos::kernel::vfs::can_modify_directory(receipts));
        write_check_result("receipt path validates", tinyos::kernel::vfs::validate_path(InstallReceiptPath));
        tinyos::drivers::vga::write("Receipt current: ");
        tinyos::drivers::vga::write_line(install_receipt_current() ? "yes" : "not written");
        tinyos::drivers::vga::write("Receipt path: ");
        tinyos::drivers::vga::write_line(InstallReceiptPath);
        tinyos::drivers::vga::write_line(installer_mock_preflight_passes() ? "Install mock preflight passed." : "Install mock preflight failed.");
    }

    void run_install_mock()
    {
        tinyos::drivers::vga::write_line("TinyOS terminal installer mock:");
        tinyos::drivers::vga::write_line("  disk writes are disabled in this stage");
        tinyos::drivers::vga::write("  receipt: ");
        tinyos::drivers::vga::write_line(InstallReceiptPath);

        if (!write_install_receipt())
        {
            tinyos::drivers::vga::write_line("Install mock failed.");
            return;
        }

        tinyos::drivers::vga::write_line("Install mock receipt written.");
        show_file(InstallReceiptPath);
    }

    void print_terminal_status()
    {
        const auto& arch_info = tinyos::arch::info();
        tinyos::drivers::vga::write_line("TinyOS terminal status:");
        tinyos::drivers::vga::write("Version       : ");
        tinyos::drivers::vga::write(tinyos::config::Name);
        tinyos::drivers::vga::write(" ");
        tinyos::drivers::vga::write_line(tinyos::config::Version);
        tinyos::drivers::vga::write("Architecture  : ");
        tinyos::drivers::vga::write_line(arch_info.name);
        tinyos::drivers::vga::write("CPU family    : ");
        tinyos::drivers::vga::write_line(arch_info.cpu_family);
        tinyos::drivers::vga::write("Current path  : ");
        tinyos::drivers::vga::write_line(g_current_directory);
        tinyos::drivers::vga::write("PIT ticks     : ");
        write_uint64(tinyos::drivers::pit::ticks());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Scheduler ticks: ");
        write_uint64(tinyos::kernel::sched::tick_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Usable memory : ");
        write_uint64(tinyos::kernel::memory::map::usable_bytes() / (1024 * 1024));
        tinyos::drivers::vga::write_line(" MiB");
        tinyos::drivers::vga::write("Frames free   : ");
        write_uint64(tinyos::kernel::memory::frames::free_frames());
        tinyos::drivers::vga::write("/");
        write_uint64(tinyos::kernel::memory::frames::total_frames());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Heap free     : ");
        write_uint64(tinyos::kernel::memory::heap::free_bytes());
        tinyos::drivers::vga::write("/");
        write_uint64(tinyos::kernel::memory::heap::total_bytes());
        tinyos::drivers::vga::write_line(" bytes");
        tinyos::drivers::vga::write("RAMFS ready   : ");
        write_yes_no(tinyos::kernel::vfs::ramfs::is_ready());
        tinyos::drivers::vga::write("Block mount   : ");
        write_yes_no(tinyos::kernel::vfs::block_mount_ready());
        tinyos::drivers::vga::write("Tools ready   : ");
        write_uint64(tinyos::kernel::admin::tools::ready_count());
        tinyos::drivers::vga::write("/");
        write_uint64(tinyos::kernel::admin::tools::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Packages      : ");
        write_uint64(tinyos::kernel::app::package::count());
        tinyos::drivers::vga::write(" known, ");
        write_uint64(tinyos::kernel::app::package::launch_ready_count());
        tinyos::drivers::vga::write_line(" launch-ready");
        tinyos::drivers::vga::write("Install receipt: ");
        tinyos::drivers::vga::write_line(install_receipt_current() ? "current" : "not written");
        tinyos::drivers::vga::write("System profile : ");
        tinyos::drivers::vga::write_line(system_profile_contract_passes() ? "valid" : "invalid");
    }

        void print_system_information()
        {
        const auto& arch_info = tinyos::arch::info();
        tinyos::drivers::vga::write_line("TinyOS system information:");
        tinyos::drivers::vga::write("System       : ");
        tinyos::drivers::vga::write(tinyos::config::Name);
        tinyos::drivers::vga::write(" ");
        tinyos::drivers::vga::write_line(tinyos::config::Version);
        tinyos::drivers::vga::write("Owner        : ");
        tinyos::drivers::vga::write_line(tinyos::config::Owner);
        tinyos::drivers::vga::write("Author       : ");
        tinyos::drivers::vga::write_line(tinyos::config::Author);
        tinyos::drivers::vga::write("License      : ");
        tinyos::drivers::vga::write_line(tinyos::config::License);
        tinyos::drivers::vga::write("Architecture : ");
        tinyos::drivers::vga::write_line(arch_info.name);
        tinyos::drivers::vga::write("CPU family   : ");
        tinyos::drivers::vga::write_line(arch_info.cpu_family);
        tinyos::drivers::vga::write("Boot media   : ");
        tinyos::drivers::vga::write_line("GRUB Multiboot ISO");
        tinyos::drivers::vga::write("Build profile: ");
    #if defined(TINYOS_TERMINAL_ONLY)
        tinyos::drivers::vga::write_line("terminal-only low-memory");
    #else
        tinyos::drivers::vga::write_line("desktop-capable terminal-first");
    #endif
        tinyos::drivers::vga::write("Shell        : ");
        tinyos::drivers::vga::write_line("kernel terminal shell");
        tinyos::drivers::vga::write("File manager : ");
        tinyos::drivers::vga::write_line("filemgr two-pane plus fileui single-pane");
        tinyos::drivers::vga::write("Text editor  : ");
        tinyos::drivers::vga::write_line("textedit interactive RAMFS editor plus edit/write");
        tinyos::drivers::vga::write("RAM baseline : ");
        tinyos::drivers::vga::write_line("32MiB supported, 3MiB+ practical probe range");
        tinyos::drivers::vga::write("Usable RAM   : ");
        write_uint64(tinyos::kernel::memory::map::usable_bytes() / 1024);
        tinyos::drivers::vga::write_line(" KiB");
        tinyos::drivers::vga::write("Profile      : ");
        tinyos::drivers::vga::write_line(SystemProfilePath);
        }

    bool syscall_contract_valid()
    {
        return tinyos::kernel::syscall::validation_self_test() &&
            tinyos::kernel::syscall::boundary_policy_validation_self_test() &&
            tinyos::kernel::syscall::definition_validation_self_test() &&
            tinyos::kernel::syscall::filter_policy_validation_self_test() &&
            tinyos::kernel::syscall::resource_policy_validation_self_test() &&
            tinyos::kernel::syscall::scheduling_validation_self_test();
    }

    void run_system_check()
    {
        size_t passed = 0;
        size_t failed = 0;
        tinyos::drivers::vga::write_line("TinyOS non-destructive system check:");
        record_self_test_result("architecture manifest", tinyos::arch::validation_self_test(), passed, failed);
        record_self_test_result("system requirements manifest", tinyos::kernel::platform::requirements::validation_self_test(), passed, failed);
        record_self_test_result("PC platform contract", tinyos::kernel::platform::pc::validation_self_test(), passed, failed);
        record_self_test_result("PC required device classes", tinyos::kernel::platform::pc::device_contract_satisfied(), passed, failed);
        record_self_test_result("PIT configured", tinyos::drivers::pit::is_configured(), passed, failed);
        record_self_test_result("scheduler ready", tinyos::kernel::sched::is_ready(), passed, failed);
        record_self_test_result("scheduler round-robin policy", tinyos::kernel::sched::validation_self_test(), passed, failed);
        record_self_test_result("scheduler sleep/wake", tinyos::kernel::sched::sleep_wake_validation_self_test(), passed, failed);
        record_self_test_result("frame allocator accounting", tinyos::kernel::memory::frames::accounting_valid(), passed, failed);
        record_self_test_result("heap state", tinyos::kernel::memory::heap::state_valid(), passed, failed);
        record_self_test_result("address space contract", tinyos::kernel::memory::address_space::validation_self_test(), passed, failed);
        record_self_test_result("paging contract", tinyos::kernel::memory::paging::validation_self_test(), passed, failed);
        record_self_test_result("runtime paging enabled", tinyos::kernel::memory::paging::is_runtime_enabled(), passed, failed);
        record_self_test_result("runtime paging policy", tinyos::kernel::memory::address_space::runtime_paging_policy_validation_self_test(), passed, failed);
        record_self_test_result("VFS path validation", tinyos::kernel::vfs::validation_self_test(), passed, failed);
        record_self_test_result("RAMFS ready", tinyos::kernel::vfs::ramfs::is_ready(), passed, failed);
        record_self_test_result("block VFS contract", tinyos::kernel::vfs::blockfs::validation_self_test(), passed, failed);
        record_self_test_result("VFS mount registry", tinyos::kernel::vfs::mount::validation_self_test(), passed, failed);
        record_self_test_result("persistent layout mount", tinyos::kernel::vfs::mount::layout_validation_self_test(), passed, failed);
        record_self_test_result("boot modules valid", tinyos::kernel::initrd::modules::validation_passed(), passed, failed);
        record_self_test_result("initrd boot VFS", tinyos::kernel::initrd::modules::vfs_validation_self_test(), passed, failed);
        record_self_test_result("ELF loader contract", tinyos::kernel::elf::loader::validation_self_test(), passed, failed);
        record_self_test_result("syscall contract bundle", syscall_contract_valid(), passed, failed);
        record_self_test_result("runtime manifest", tinyos::kernel::app::runtime::validation_self_test(), passed, failed);
        record_self_test_result("application manifest", tinyos::kernel::app::manifest::validation_self_test(), passed, failed);
        record_self_test_result("TAPP registry", tinyos::kernel::app::package::validation_self_test(), passed, failed);
        record_self_test_result("TAPP trust store", tinyos::kernel::security::trust::validation_self_test(), passed, failed);
        record_self_test_result("TAPP package verifier", tinyos::kernel::app::package_verifier::validation_self_test(), passed, failed);
        record_self_test_result("application launcher", tinyos::kernel::app::launcher::validation_self_test(), passed, failed);
        record_self_test_result("initial process contract", tinyos::kernel::user::transition::validation_self_test(), passed, failed);
        record_self_test_result("system management tools", tinyos::kernel::admin::tools::validation_self_test(), passed, failed);
        record_self_test_result("secure provisioning manifest", tinyos::kernel::provision::image::validation_self_test(), passed, failed);
        record_self_test_result("system profile contract", system_profile_contract_passes(), passed, failed);
        record_self_test_result("installer mock preflight", installer_mock_preflight_passes(), passed, failed);
        record_self_test_result("terminal UI contract", tinyos::ui::terminal::validation_self_test(), passed, failed);
        record_self_test_result("terminal panel contract", tinyos::ui::terminal::panel_validation_self_test(), passed, failed);
        record_self_test_result("terminal style contract", tinyos::ui::terminal::style_validation_self_test(), passed, failed);
        record_self_test_result("TUI widget contract", tinyos::ui::widgets::validation_self_test(), passed, failed);
        record_self_test_result("TUI event bridge", tinyos::ui::widgets::event_bridge_validation_self_test(), passed, failed);
        tinyos::drivers::vga::write("Passed: ");
        write_uint64(passed);
        tinyos::drivers::vga::write(" Failed: ");
        write_uint64(failed);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write_line(failed == 0 ? "System check passed." : "System check failed.");
    }

    void print_path_check(const char* path_text)
    {
        char path[MaxPathLength];
        const char* rest = nullptr;
        if (!copy_argument(path_text, path, sizeof(path), rest))
        {
            tinyos::drivers::vga::write_line("Usage: pathcheck <path>");
            return;
        }

        char resolved[MaxPathLength];
        const bool resolved_ok = resolve_shell_path(path, resolved, sizeof(resolved));
        tinyos::drivers::vga::write_line("TinyOS path check:");
        tinyos::drivers::vga::write("Input    : ");
        tinyos::drivers::vga::write_line(path);
        tinyos::drivers::vga::write("Resolved : ");
        tinyos::drivers::vga::write_line(resolved_ok ? resolved : "invalid");
        tinyos::drivers::vga::write("Valid    : ");
        write_yes_no(resolved_ok);
        if (!resolved_ok)
        {
            return;
        }

        const auto* node = tinyos::kernel::vfs::find(resolved);
        tinyos::drivers::vga::write("Exists   : ");
        write_yes_no(node != nullptr);
        if (node == nullptr)
        {
            return;
        }

        tinyos::drivers::vga::write("Type     : ");
        tinyos::drivers::vga::write_line(node->directory ? "directory" : "file");
        tinyos::drivers::vga::write("Writable : ");
        write_yes_no(node->writable);
        tinyos::drivers::vga::write("Mode     : ");
        write_octal_mode(tinyos::kernel::vfs::access_mode(node));
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Can enter: ");
        write_yes_no(tinyos::kernel::vfs::can_enter_directory(node));
        tinyos::drivers::vga::write("Can list : ");
        write_yes_no(tinyos::kernel::vfs::can_list_directory(node));
        tinyos::drivers::vga::write("Can modify: ");
        write_yes_no(tinyos::kernel::vfs::can_modify_directory(node));
        if (!node->directory)
        {
            tinyos::drivers::vga::write("Size     : ");
            write_uint64(node->size);
            tinyos::drivers::vga::write("/");
            write_uint64(node->capacity);
            tinyos::drivers::vga::write_line(" bytes");
        }
    }

    void cleanup_fs_self_test_paths()
    {
        (void)tinyos::kernel::vfs::set_access_mode("/users/fstest/source.txt", 0600);
        (void)tinyos::kernel::vfs::set_access_mode("/users/fstest/copy.txt", 0600);
        (void)tinyos::kernel::vfs::set_access_mode("/users/fstest/moved.txt", 0600);
        (void)tinyos::kernel::vfs::set_access_mode("/users/fstest", 0700);
        (void)tinyos::kernel::vfs::remove("/users/fstest/moved.txt");
        (void)tinyos::kernel::vfs::remove("/users/fstest/copy.txt");
        (void)tinyos::kernel::vfs::remove("/users/fstest/source.txt");
        (void)tinyos::kernel::vfs::remove("/users/fstest");
    }

    bool run_fs_self_test()
    {
        size_t passed = 0;
        size_t failed = 0;
        constexpr const char* DirectoryPath = "/users/fstest";
        constexpr const char* SourcePath = "/users/fstest/source.txt";
        constexpr const char* CopyPath = "/users/fstest/copy.txt";
        constexpr const char* MovedPath = "/users/fstest/moved.txt";

        cleanup_fs_self_test_paths();
        tinyos::drivers::vga::write_line("RAMFS file operation self-test:");
        record_self_test_result("create directory", tinyos::kernel::vfs::create_directory(DirectoryPath), passed, failed);
        record_self_test_result("directory visible", tinyos::kernel::vfs::find(DirectoryPath) != nullptr, passed, failed);
        record_self_test_result("create file", tinyos::kernel::vfs::create_file(SourcePath), passed, failed);
        record_self_test_result("write file", tinyos::kernel::vfs::write_file(SourcePath, "alpha", 5), passed, failed);
        record_self_test_result("read written file", file_contains(SourcePath, "alpha"), passed, failed);
        record_self_test_result("copy file", tinyos::kernel::vfs::copy_file(SourcePath, CopyPath), passed, failed);
        record_self_test_result("read copied file", file_contains(CopyPath, "alpha"), passed, failed);
        record_self_test_result("move copied file", tinyos::kernel::vfs::move(CopyPath, MovedPath), passed, failed);
        record_self_test_result("old copy path removed", tinyos::kernel::vfs::find(CopyPath) == nullptr, passed, failed);
        record_self_test_result("moved file visible", file_contains(MovedPath, "alpha"), passed, failed);
        record_self_test_result("reject non-empty directory remove", !tinyos::kernel::vfs::remove(DirectoryPath), passed, failed);
        record_self_test_result("set read-only mode", tinyos::kernel::vfs::set_access_mode(SourcePath, 0400), passed, failed);
        record_self_test_result("reject write without owner write bit", !tinyos::kernel::vfs::write_file(SourcePath, "blocked", 7), passed, failed);
        record_self_test_result("restore writable mode", tinyos::kernel::vfs::set_access_mode(SourcePath, 0600), passed, failed);
        record_self_test_result("rewrite writable file", tinyos::kernel::vfs::write_file(SourcePath, "omega", 5), passed, failed);
        record_self_test_result("read rewritten file", file_contains(SourcePath, "omega"), passed, failed);
        record_self_test_result("remove moved file", tinyos::kernel::vfs::remove(MovedPath), passed, failed);
        record_self_test_result("remove source file", tinyos::kernel::vfs::remove(SourcePath), passed, failed);
        record_self_test_result("set no-read directory mode", tinyos::kernel::vfs::set_access_mode(DirectoryPath, 0300), passed, failed);
        const auto* no_read_directory = tinyos::kernel::vfs::find(DirectoryPath);
        record_self_test_result("deny directory listing without owner read bit", !tinyos::kernel::vfs::can_list_directory(no_read_directory) && tinyos::kernel::vfs::child_count(no_read_directory) == 0, passed, failed);
        record_self_test_result("allow directory mutation with write and execute bits", tinyos::kernel::vfs::create_file(SourcePath), passed, failed);
        record_self_test_result("remove file from no-read directory", tinyos::kernel::vfs::remove(SourcePath), passed, failed);
        record_self_test_result("set no-execute directory mode", tinyos::kernel::vfs::set_access_mode(DirectoryPath, 0600), passed, failed);
        const auto* no_execute_directory = tinyos::kernel::vfs::find(DirectoryPath);
        record_self_test_result("deny directory enter without owner execute bit", !tinyos::kernel::vfs::can_enter_directory(no_execute_directory), passed, failed);
        record_self_test_result("deny directory mutation without owner execute bit", !tinyos::kernel::vfs::create_file(SourcePath), passed, failed);
        record_self_test_result("restore directory mode", tinyos::kernel::vfs::set_access_mode(DirectoryPath, 0700), passed, failed);
        record_self_test_result("remove empty directory", tinyos::kernel::vfs::remove(DirectoryPath), passed, failed);

        cleanup_fs_self_test_paths();
        tinyos::drivers::vga::write("Passed: ");
        write_uint64(passed);
        tinyos::drivers::vga::write(" Failed: ");
        write_uint64(failed);
        tinyos::drivers::vga::put_char('\n');
        return failed == 0;
    }

    size_t clamped_fileui_selection(const char* path, size_t selected)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const size_t count = tinyos::kernel::vfs::child_count(node);
        if (count == 0)
        {
            return 0;
        }

        return selected < count ? selected : count - 1;
    }

    const tinyos::kernel::vfs::Node* selected_fileui_node(const char* path, size_t selected)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr || !node->directory)
        {
            return nullptr;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            return nullptr;
        }

        return tinyos::kernel::vfs::child_at(node, selected);
    }

    void draw_file_ui(const char* path, size_t selected)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const size_t count = tinyos::kernel::vfs::child_count(node);
        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write_line("TinyOS FileUI");
        tinyos::drivers::vga::write_line("Up/Down select, Enter/Right open, Left parent, Q/Esc exit");
        tinyos::drivers::vga::write_line("N file, M dir, E edit, D remove, C copy, R move");
        tinyos::drivers::vga::write("Path: ");
        tinyos::drivers::vga::write_line(path);
        tinyos::drivers::vga::write_line("");

        if (node == nullptr || !node->directory)
        {
            tinyos::drivers::vga::write_line("Directory not found.");
            return;
        }

        if (!tinyos::kernel::vfs::can_list_directory(node))
        {
            tinyos::drivers::vga::write_line("Directory not readable.");
            return;
        }

        if (count == 0)
        {
            tinyos::drivers::vga::write_line("<empty>");
            return;
        }

        size_t first = 0;
        constexpr size_t VisibleRows = 15;
        if (selected >= VisibleRows)
        {
            first = selected - VisibleRows + 1;
        }

        for (size_t offset = 0; offset < VisibleRows && first + offset < count; ++offset)
        {
            const size_t index = first + offset;
            const auto* child = tinyos::kernel::vfs::child_at(node, index);
            tinyos::drivers::vga::write(index == selected ? "> " : "  ");
            print_node_entry(child);
        }
    }

    bool fileui_selected_path(const char* directory, size_t selected, char* output, size_t output_size)
    {
        const auto* child = selected_fileui_node(directory, selected);
        return child != nullptr && child->name != nullptr && build_child_path(directory, child->name, output, output_size);
    }

    bool prompt_fileui_path(const char* base, const char* prompt, char* output, size_t output_size)
    {
        char input[MaxInputLength];
        tinyos::drivers::vga::write(prompt);
        tinyos::drivers::keyboard::read_line(input, sizeof(input));
        if (input[0] == '\0')
        {
            return false;
        }

        return resolve_path_from(base, input, output, output_size);
    }

    bool confirm_fileui_action(const char* prompt)
    {
        tinyos::drivers::vga::write(prompt);
        tinyos::drivers::vga::write(" [y/N] ");
        const char key = tinyos::drivers::keyboard::read_char();
        tinyos::drivers::vga::put_char('\n');
        return key == 'y' || key == 'Y';
    }

    void fileui_message(const char* message)
    {
        tinyos::drivers::vga::write_line(message);
        wait_for_key();
    }

    bool load_textedit_buffer(const char* path, size_t& buffer_size)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        const char* data = nullptr;
        size_t size = 0;
        buffer_size = 0;
        if (node == nullptr || node->directory || !tinyos::kernel::vfs::read_file(node, data, size) || size > TextEditorBufferBytes)
        {
            g_textedit_buffer[0] = '\0';
            return false;
        }

        for (size_t index = 0; index < size; ++index)
        {
            g_textedit_buffer[index] = data[index];
        }

        g_textedit_buffer[size] = '\0';
        buffer_size = size;
        return true;
    }

    bool save_textedit_buffer(const char* path, size_t buffer_size)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        if (node == nullptr || node->directory || !node->writable || buffer_size > node->capacity)
        {
            return false;
        }

        return tinyos::kernel::vfs::write_file(path, g_textedit_buffer, buffer_size);
    }

    bool replace_textedit_buffer(const char* text, size_t& buffer_size)
    {
        const size_t text_size = tinyos::core::string::length(text);
        if (text_size > TextEditorBufferBytes)
        {
            return false;
        }

        for (size_t index = 0; index < text_size; ++index)
        {
            g_textedit_buffer[index] = text[index];
        }

        g_textedit_buffer[text_size] = '\0';
        buffer_size = text_size;
        return true;
    }

    bool append_textedit_line(const char* text, size_t& buffer_size)
    {
        const size_t text_size = tinyos::core::string::length(text);
        const size_t newline = buffer_size == 0 || g_textedit_buffer[buffer_size - 1] == '\n' ? 0 : 1;
        if (buffer_size + newline + text_size + 1 > TextEditorBufferBytes)
        {
            return false;
        }

        if (newline != 0)
        {
            g_textedit_buffer[buffer_size] = '\n';
            ++buffer_size;
        }

        for (size_t index = 0; index < text_size; ++index)
        {
            g_textedit_buffer[buffer_size + index] = text[index];
        }

        buffer_size += text_size;
        g_textedit_buffer[buffer_size] = '\n';
        ++buffer_size;
        g_textedit_buffer[buffer_size] = '\0';
        return true;
    }

    void draw_text_editor(const char* path, size_t buffer_size, bool dirty)
    {
        const auto* node = tinyos::kernel::vfs::find(path);
        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write_line("TinyOS TextEdit");
        tinyos::drivers::vga::write_line("E replace  A append  C clear  S save  R reload  I info  Q exit");
        tinyos::drivers::vga::write("Path: ");
        tinyos::drivers::vga::write_line(path);
        tinyos::drivers::vga::write("State: ");
        tinyos::drivers::vga::write(node != nullptr && node->writable ? "writable" : "read-only");
        tinyos::drivers::vga::write(dirty ? ", modified" : ", clean");
        tinyos::drivers::vga::write("  Size: ");
        write_uint64(buffer_size);
        tinyos::drivers::vga::write("/");
        write_uint64(node != nullptr ? node->capacity : 0);
        tinyos::drivers::vga::write_line(" bytes");
        tinyos::drivers::vga::write_line("----------------------------------------");
        if (buffer_size == 0)
        {
            tinyos::drivers::vga::write_line("<empty>");
        }
        else
        {
            write_buffer(g_textedit_buffer, buffer_size);
            if (g_textedit_buffer[buffer_size - 1] != '\n')
            {
                tinyos::drivers::vga::put_char('\n');
            }
        }
        tinyos::drivers::vga::write_line("----------------------------------------");
    }

    void run_text_editor(const char* path)
    {
        if (tinyos::kernel::vfs::find(path) == nullptr)
        {
            tinyos::drivers::vga::write("File not found. Create it? [y/N] ");
            const char key = tinyos::drivers::keyboard::read_char();
            tinyos::drivers::vga::put_char('\n');
            if ((key != 'y' && key != 'Y') || !tinyos::kernel::vfs::create_file(path))
            {
                tinyos::drivers::vga::write_line("TextEdit aborted.");
                return;
            }
        }

        size_t buffer_size = 0;
        bool dirty = false;
        if (!load_textedit_buffer(path, buffer_size))
        {
            tinyos::drivers::vga::write_line("TextEdit cannot open this file.");
            return;
        }

        for (;;)
        {
            draw_text_editor(path, buffer_size, dirty);
            const char key = tinyos::drivers::keyboard::read_char();
            if (key == 'q' || key == 'Q' || key == 27)
            {
                if (dirty && !confirm_fileui_action("Discard unsaved text?"))
                {
                    continue;
                }
                tinyos::drivers::vga::clear();
                return;
            }
            if (key == 'e' || key == 'E')
            {
                tinyos::drivers::vga::write("Replace with: ");
                tinyos::drivers::keyboard::read_line(g_textedit_line, sizeof(g_textedit_line));
                if (!replace_textedit_buffer(g_textedit_line, buffer_size))
                {
                    fileui_message("Replacement is too large.");
                }
                else
                {
                    dirty = true;
                }
                continue;
            }
            if (key == 'a' || key == 'A')
            {
                tinyos::drivers::vga::write("Append line: ");
                tinyos::drivers::keyboard::read_line(g_textedit_line, sizeof(g_textedit_line));
                if (!append_textedit_line(g_textedit_line, buffer_size))
                {
                    fileui_message("Append would exceed file buffer.");
                }
                else
                {
                    dirty = true;
                }
                continue;
            }
            if (key == 'c' || key == 'C')
            {
                if (confirm_fileui_action("Clear editor buffer?"))
                {
                    g_textedit_buffer[0] = '\0';
                    buffer_size = 0;
                    dirty = true;
                }
                continue;
            }
            if (key == 's' || key == 'S')
            {
                if (save_textedit_buffer(path, buffer_size))
                {
                    dirty = false;
                    fileui_message("File saved.");
                }
                else
                {
                    fileui_message("Save failed.");
                }
                continue;
            }
            if (key == 'r' || key == 'R')
            {
                if (!dirty || confirm_fileui_action("Reload and discard changes?"))
                {
                    dirty = false;
                    (void)load_textedit_buffer(path, buffer_size);
                }
                continue;
            }
            if (key == 'i' || key == 'I')
            {
                tinyos::drivers::vga::clear();
                show_file_info(path);
                wait_for_key();
                continue;
            }
        }
    }

    void print_filemgr_entry(const tinyos::kernel::vfs::Node* node, bool selected, bool active)
    {
        tinyos::drivers::vga::write(selected ? (active ? "> " : "* ") : "  ");
        tinyos::drivers::vga::write(node != nullptr && node->directory ? "[D] " : "[F] ");
        const char* name = node != nullptr && node->name != nullptr ? node->name : "invalid";
        size_t index = 0;
        while (name[index] != '\0' && index < 24)
        {
            tinyos::drivers::vga::put_char(name[index]);
            ++index;
        }
        while (index < 24)
        {
            tinyos::drivers::vga::put_char(' ');
            ++index;
        }
    }

    void draw_file_manager(const char* left_path, size_t left_selected, const char* right_path, size_t right_selected, bool left_active)
    {
        const auto* left_node = tinyos::kernel::vfs::find(left_path);
        const auto* right_node = tinyos::kernel::vfs::find(right_path);
        const size_t left_count = tinyos::kernel::vfs::child_count(left_node);
        const size_t right_count = tinyos::kernel::vfs::child_count(right_node);
        constexpr size_t VisibleRows = 14;
        size_t left_first = 0;
        size_t right_first = 0;
        if (left_selected >= VisibleRows)
        {
            left_first = left_selected - VisibleRows + 1;
        }
        if (right_selected >= VisibleRows)
        {
            right_first = right_selected - VisibleRows + 1;
        }

        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write_line("TinyOS FileMgr");
        tinyos::drivers::vga::write_line("Tab switch  Enter open/view  Left parent  V view  E edit  N file  M dir");
        tinyos::drivers::vga::write_line("C copy to other pane  R move to other pane  D delete  Q exit");
        tinyos::drivers::vga::write(left_active ? "Left* : " : "Left  : ");
        tinyos::drivers::vga::write(left_path);
        tinyos::drivers::vga::write("    ");
        tinyos::drivers::vga::write(left_active ? "Right : " : "Right*: ");
        tinyos::drivers::vga::write_line(right_path);

        for (size_t offset = 0; offset < VisibleRows; ++offset)
        {
            const size_t left_index = left_first + offset;
            const size_t right_index = right_first + offset;
            print_filemgr_entry(left_index < left_count ? tinyos::kernel::vfs::child_at(left_node, left_index) : nullptr, left_index == left_selected && left_count != 0, left_active);
            tinyos::drivers::vga::write(" | ");
            print_filemgr_entry(right_index < right_count ? tinyos::kernel::vfs::child_at(right_node, right_index) : nullptr, right_index == right_selected && right_count != 0, !left_active);
            tinyos::drivers::vga::put_char('\n');
        }
    }

    void filemgr_open_selected(char* path, size_t& selected)
    {
        char selected_path[MaxPathLength];
        const auto* child = selected_fileui_node(path, selected);
        if (child == nullptr || !build_child_path(path, child->name, selected_path, sizeof(selected_path)))
        {
            fileui_message("Nothing selected.");
            return;
        }

        if (child->directory)
        {
            if (!tinyos::kernel::vfs::can_enter_directory(child))
            {
                fileui_message("Directory not executable.");
                return;
            }
            (void)copy_path_string(path, MaxPathLength, selected_path);
            selected = 0;
            return;
        }

        tinyos::drivers::vga::clear();
        show_file_info(selected_path);
        tinyos::drivers::vga::write_line("");
        show_file(selected_path);
        wait_for_key();
    }

    void filemgr_copy_or_move_to_other(const char* source_directory, size_t selected, const char* destination_directory, bool move_path)
    {
        char source_path[MaxPathLength];
        char destination_path[MaxPathLength];
        const auto* child = selected_fileui_node(source_directory, selected);
        if (child == nullptr || child->directory || !fileui_selected_path(source_directory, selected, source_path, sizeof(source_path)) || !build_child_path(destination_directory, child->name, destination_path, sizeof(destination_path)))
        {
            fileui_message("Select a file first.");
            return;
        }

        const bool ok = move_path ? tinyos::kernel::vfs::move(source_path, destination_path) : tinyos::kernel::vfs::copy_file(source_path, destination_path);
        fileui_message(ok ? (move_path ? "File moved to other pane." : "File copied to other pane.") : (move_path ? "Move failed." : "Copy failed."));
    }

    void run_file_manager()
    {
        char left_path[MaxPathLength];
        char right_path[MaxPathLength];
        (void)copy_path_string(left_path, sizeof(left_path), g_current_directory);
        (void)copy_path_string(right_path, sizeof(right_path), "/");
        size_t left_selected = 0;
        size_t right_selected = 0;
        bool left_active = true;

        for (;;)
        {
            left_selected = clamped_fileui_selection(left_path, left_selected);
            right_selected = clamped_fileui_selection(right_path, right_selected);
            draw_file_manager(left_path, left_selected, right_path, right_selected, left_active);
            const char key = tinyos::drivers::keyboard::read_char();
            char* active_path = left_active ? left_path : right_path;
            char* other_path = left_active ? right_path : left_path;
            size_t& active_selected = left_active ? left_selected : right_selected;
            const auto* active_node = tinyos::kernel::vfs::find(active_path);
            const size_t active_count = tinyos::kernel::vfs::child_count(active_node);

            if (key == 'q' || key == 'Q' || key == 27)
            {
                tinyos::drivers::vga::clear();
                return;
            }
            if (key == '\t')
            {
                left_active = !left_active;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyUp && active_count != 0)
            {
                active_selected = active_selected == 0 ? active_count - 1 : active_selected - 1;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyDown && active_count != 0)
            {
                active_selected = (active_selected + 1) % active_count;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyLeft)
            {
                (void)pop_path_segment(active_path);
                active_selected = 0;
                continue;
            }
            if ((key == tinyos::drivers::keyboard::KeyRight || key == '\n') && active_count != 0)
            {
                filemgr_open_selected(active_path, active_selected);
                continue;
            }
            if ((key == 'v' || key == 'V') && active_count != 0)
            {
                fileui_show_selected(active_path, active_selected);
                continue;
            }
            if ((key == 'e' || key == 'E') && active_count != 0)
            {
                char selected_path[MaxPathLength];
                if (fileui_selected_path(active_path, active_selected, selected_path, sizeof(selected_path)))
                {
                    run_text_editor(selected_path);
                }
                continue;
            }
            if (key == 'n' || key == 'N')
            {
                char path[MaxPathLength];
                if (prompt_fileui_path(active_path, "New file: ", path, sizeof(path)))
                {
                    fileui_message(tinyos::kernel::vfs::create_file(path) ? "File created." : "File create failed.");
                }
                continue;
            }
            if (key == 'm' || key == 'M')
            {
                char path[MaxPathLength];
                if (prompt_fileui_path(active_path, "New directory: ", path, sizeof(path)))
                {
                    fileui_message(tinyos::kernel::vfs::create_directory(path) ? "Directory created." : "Directory create failed.");
                }
                continue;
            }
            if ((key == 'd' || key == 'D') && active_count != 0)
            {
                char selected_path[MaxPathLength];
                if (fileui_selected_path(active_path, active_selected, selected_path, sizeof(selected_path)) && confirm_fileui_action("Remove selected path?"))
                {
                    fileui_message(tinyos::kernel::vfs::remove(selected_path) ? "Path removed." : "Remove failed.");
                }
                continue;
            }
            if ((key == 'c' || key == 'C') && active_count != 0)
            {
                filemgr_copy_or_move_to_other(active_path, active_selected, other_path, false);
                continue;
            }
            if ((key == 'r' || key == 'R') && active_count != 0)
            {
                filemgr_copy_or_move_to_other(active_path, active_selected, other_path, true);
                continue;
            }
        }
    }

    void fileui_show_selected(const char* current_path, size_t selected)
    {
        char selected_path[MaxPathLength];
        if (!fileui_selected_path(current_path, selected, selected_path, sizeof(selected_path)))
        {
            fileui_message("Nothing selected.");
            return;
        }

        tinyos::drivers::vga::clear();
        show_file_info(selected_path);
        tinyos::drivers::vga::write_line("");
        show_file(selected_path);
        wait_for_key();
    }

    void run_file_ui()
    {
        char current_path[MaxPathLength];
        if (!copy_path_string(current_path, sizeof(current_path), g_current_directory))
        {
            (void)copy_path_string(current_path, sizeof(current_path), "/");
        }

        size_t selected = 0;
        for (;;)
        {
            selected = clamped_fileui_selection(current_path, selected);
            draw_file_ui(current_path, selected);
            const char key = tinyos::drivers::keyboard::read_char();
            const auto* current_node = tinyos::kernel::vfs::find(current_path);
            const size_t count = tinyos::kernel::vfs::child_count(current_node);

            if (key == 'q' || key == 'Q' || key == 27)
            {
                tinyos::drivers::vga::clear();
                return;
            }
            if (key == tinyos::drivers::keyboard::KeyUp && count != 0)
            {
                selected = selected == 0 ? count - 1 : selected - 1;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyDown && count != 0)
            {
                selected = (selected + 1) % count;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyLeft)
            {
                (void)pop_path_segment(current_path);
                selected = 0;
                continue;
            }
            if ((key == tinyos::drivers::keyboard::KeyRight || key == '\n') && count != 0)
            {
                char selected_path[MaxPathLength];
                const auto* child = selected_fileui_node(current_path, selected);
                if (child != nullptr && build_child_path(current_path, child->name, selected_path, sizeof(selected_path)))
                {
                    if (child->directory)
                    {
                        if (!tinyos::kernel::vfs::can_enter_directory(child))
                        {
                            fileui_message("Directory not executable.");
                            continue;
                        }

                        (void)copy_path_string(current_path, sizeof(current_path), selected_path);
                        selected = 0;
                    }
                    else
                    {
                        fileui_show_selected(current_path, selected);
                    }
                }
                continue;
            }
            if (key == 'n' || key == 'N')
            {
                char path[MaxPathLength];
                if (prompt_fileui_path(current_path, "New file: ", path, sizeof(path)))
                {
                    fileui_message(tinyos::kernel::vfs::create_file(path) ? "File created." : "File create failed.");
                }
                continue;
            }
            if (key == 'm' || key == 'M')
            {
                char path[MaxPathLength];
                if (prompt_fileui_path(current_path, "New directory: ", path, sizeof(path)))
                {
                    fileui_message(tinyos::kernel::vfs::create_directory(path) ? "Directory created." : "Directory create failed.");
                }
                continue;
            }
            if ((key == 'e' || key == 'E') && count != 0)
            {
                char selected_path[MaxPathLength];
                if (fileui_selected_path(current_path, selected, selected_path, sizeof(selected_path)))
                {
                    run_text_editor(selected_path);
                }
                continue;
            }
            if ((key == 'd' || key == 'D') && count != 0)
            {
                char selected_path[MaxPathLength];
                if (fileui_selected_path(current_path, selected, selected_path, sizeof(selected_path)) && confirm_fileui_action("Remove selected path?"))
                {
                    fileui_message(tinyos::kernel::vfs::remove(selected_path) ? "Path removed." : "Remove failed.");
                }
                continue;
            }
            if ((key == 'c' || key == 'C') && count != 0)
            {
                char selected_path[MaxPathLength];
                char destination[MaxPathLength];
                if (fileui_selected_path(current_path, selected, selected_path, sizeof(selected_path)) && prompt_fileui_path(current_path, "Copy to: ", destination, sizeof(destination)))
                {
                    fileui_message(tinyos::kernel::vfs::copy_file(selected_path, destination) ? "File copied." : "Copy failed.");
                }
                continue;
            }
            if ((key == 'r' || key == 'R') && count != 0)
            {
                char selected_path[MaxPathLength];
                char destination[MaxPathLength];
                if (fileui_selected_path(current_path, selected, selected_path, sizeof(selected_path)) && prompt_fileui_path(current_path, "Move to: ", destination, sizeof(destination)))
                {
                    fileui_message(tinyos::kernel::vfs::move(selected_path, destination) ? "Path moved." : "Move failed.");
                }
                continue;
            }
        }
    }

    void print_device_entry(size_t index, const tinyos::kernel::device::Device& device)
    {
        tinyos::drivers::vga::write("  #");
        write_uint64(index);
        tinyos::drivers::vga::write(" ");
        tinyos::drivers::vga::write(tinyos::kernel::device::class_name(device.device_class));
        tinyos::drivers::vga::write("/");
        tinyos::drivers::vga::write(device.name != nullptr ? device.name : "unnamed");
        tinyos::drivers::vga::write(" unit=");
        write_uint64(device.unit);
        tinyos::drivers::vga::write(" state=");
        tinyos::drivers::vga::write(tinyos::kernel::device::state_name(device.state));
        tinyos::drivers::vga::write(" flags=");
        write_uint64(device.flags);
        tinyos::drivers::vga::put_char('\n');
    }

    void print_device_registry()
    {
        tinyos::drivers::vga::write("Device registry ready: ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Registered devices  : ");
        write_uint64(tinyos::kernel::device::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Ready devices       : ");
        write_uint64(tinyos::kernel::device::ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Registry capacity   : ");
        write_uint64(tinyos::kernel::device::capacity());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected entries    : ");
        write_uint64(tinyos::kernel::device::rejected_registration_count());
        tinyos::drivers::vga::put_char('\n');

        for (size_t index = 0; index < tinyos::kernel::device::count(); ++index)
        {
            const auto* device = tinyos::kernel::device::at(index);
            if (device == nullptr)
            {
                continue;
            }

            print_device_entry(index, *device);
        }
    }

    void print_device_detail(const char* name)
    {
        const auto* device = tinyos::kernel::device::find_by_name(name);
        if (device == nullptr)
        {
            tinyos::drivers::vga::write_line("Device not found.");
            return;
        }

        tinyos::drivers::vga::write("Name  : ");
        tinyos::drivers::vga::write_line(device->name != nullptr ? device->name : "unnamed");
        tinyos::drivers::vga::write("Class : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::class_name(device->device_class));
        tinyos::drivers::vga::write("State : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::state_name(device->state));
        tinyos::drivers::vga::write("Unit  : ");
        write_uint64(device->unit);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Flags : ");
        write_uint64(device->flags);
        tinyos::drivers::vga::put_char('\n');
    }

    void print_block_info()
    {
        const auto* device = tinyos::kernel::device::block::root_device();
        tinyos::drivers::vga::write("Block ready : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::block::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("VirtIO block: ");
        write_yes_no(tinyos::kernel::device::block::virtio_available());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Active name : ");
        tinyos::drivers::vga::write_line(device != nullptr && device->name != nullptr ? device->name : "none");
        const auto* ram = tinyos::kernel::device::block::ram_device();
        if (ram != nullptr)
        {
            tinyos::drivers::vga::write("RAM fallback   : ");
            tinyos::drivers::vga::write_line(ram->name != nullptr ? ram->name : "none");
        }
        tinyos::drivers::vga::write("Sector size : ");
        write_uint64(tinyos::kernel::device::block::sector_size());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Sectors     : ");
        write_uint64(tinyos::kernel::device::block::sector_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Total bytes : ");
        write_uint64(tinyos::kernel::device::block::total_size());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Writable    : ");
        tinyos::drivers::vga::write_line(device != nullptr && device->writable ? "yes" : "no");
        tinyos::drivers::vga::write("Self-test   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::block::validation_self_test() ? "ok" : "failed");
    }

    void print_storage_info()
    {
        tinyos::drivers::vga::write("Block mount ready: ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::block_mount_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Mount path       : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::blockfs::mount_path());
        tinyos::drivers::vga::write("Device           : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::blockfs::mounted_device_name());
        tinyos::drivers::vga::write("Volume path      : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::blockfs::primary_volume_path());
        tinyos::drivers::vga::write("Self-test        : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::blockfs::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Boot mount path  : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::initrd::modules::vfs_mount_path());
        tinyos::drivers::vga::write("Boot modules     : ");
        write_uint64(tinyos::kernel::initrd::modules::vfs_file_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Boot self-test   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::initrd::modules::vfs_validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Bind mounts  : ");
        write_uint64(tinyos::kernel::vfs::mount::active_count());
        tinyos::drivers::vga::put_char('\n');
        for (size_t index = 0; index < tinyos::kernel::vfs::mount::active_count(); ++index)
        {
            const char* source = nullptr;
            const char* target = nullptr;
            if (!tinyos::kernel::vfs::mount::active_at(index, source, target))
            {
                continue;
            }

            tinyos::drivers::vga::write("  ");
            tinyos::drivers::vga::write(target != nullptr ? target : "invalid");
            tinyos::drivers::vga::write(" -> ");
            tinyos::drivers::vga::write_line(source != nullptr ? source : "invalid");
        }
    }

    void print_mount_info()
    {
        tinyos::drivers::vga::write("Active mounts : ");
        write_uint64(tinyos::kernel::vfs::mount::active_count());
        tinyos::drivers::vga::put_char('\n');
        if (tinyos::kernel::vfs::mount::active_count() == 0)
        {
            tinyos::drivers::vga::write_line("No bind mounts.");
            tinyos::drivers::vga::write_line("Usage: mount <source> <target>");
            tinyos::drivers::vga::write_line("Example: mount /volumes/disk0 /mnt");
            return;
        }

        for (size_t index = 0; index < tinyos::kernel::vfs::mount::active_count(); ++index)
        {
            const char* source = nullptr;
            const char* target = nullptr;
            if (!tinyos::kernel::vfs::mount::active_at(index, source, target))
            {
                continue;
            }

            tinyos::drivers::vga::write("  ");
            tinyos::drivers::vga::write(target != nullptr ? target : "invalid");
            tinyos::drivers::vga::write(" -> ");
            tinyos::drivers::vga::write_line(source != nullptr ? source : "invalid");
        }
    }

    bool handle_mount_command(const char* command)
    {
        if (tinyos::core::string::compare(command, "mount") == 0)
        {
            print_mount_info();
            return true;
        }

        if (tinyos::core::string::starts_with(command, "mount "))
        {
            char source[MaxPathLength];
            const char* target_text = nullptr;
            if (!copy_argument(command + 5, source, sizeof(source), target_text))
            {
                tinyos::drivers::vga::write_line("Usage: mount <source> <target>");
                return true;
            }

            char target[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(target_text, target, sizeof(target), rest))
            {
                tinyos::drivers::vga::write_line("Usage: mount <source> <target>");
                return true;
            }

            char resolved_source[MaxPathLength];
            char resolved_target[MaxPathLength];
            if (!resolve_shell_path(source, resolved_source, sizeof(resolved_source)) ||
                !resolve_shell_path(target, resolved_target, sizeof(resolved_target)))
            {
                tinyos::drivers::vga::write_line("Invalid path.");
                return true;
            }

            tinyos::drivers::vga::write_line(
                tinyos::kernel::vfs::mount::mount(resolved_source, resolved_target)
                    ? "Mount succeeded."
                    : "Mount failed.");
            return true;
        }

        if (tinyos::core::string::starts_with(command, "umount ") || tinyos::core::string::starts_with(command, "unmount "))
        {
            const char* prefix = tinyos::core::string::starts_with(command, "umount ") ? "umount " : "unmount ";
            char target[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + tinyos::core::string::length(prefix), target, sizeof(target), rest))
            {
                tinyos::drivers::vga::write_line("Usage: umount <target>");
                return true;
            }

            char resolved_target[MaxPathLength];
            if (!resolve_shell_path(target, resolved_target, sizeof(resolved_target)))
            {
                tinyos::drivers::vga::write_line("Invalid path.");
                return true;
            }

            tinyos::drivers::vga::write_line(
                tinyos::kernel::vfs::mount::unmount(resolved_target)
                    ? "Unmount succeeded."
                    : "Unmount failed.");
            return true;
        }

        return false;
    }

    void print_framebuffer_info()
    {
        const auto* surface = tinyos::kernel::device::framebuffer::active_surface();
        const auto* linear = tinyos::kernel::device::framebuffer::linear_surface();
        tinyos::drivers::vga::write("Surface ready : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Name          : ");
        tinyos::drivers::vga::write_line(surface != nullptr && surface->name != nullptr ? surface->name : "none");
        tinyos::drivers::vga::write("Kind          : ");
        tinyos::drivers::vga::write_line(surface != nullptr ? tinyos::kernel::device::framebuffer::kind_name(surface->kind) : "none");
        tinyos::drivers::vga::write("Width         : ");
        write_uint64(surface != nullptr ? surface->width : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Height        : ");
        write_uint64(surface != nullptr ? surface->height : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pitch         : ");
        write_uint64(surface != nullptr ? surface->pitch : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Bits per item : ");
        write_uint64(surface != nullptr ? surface->bits_per_pixel : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Linear FB     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::has_linear_framebuffer() ? "detected" : "fallback text-grid");
        tinyos::drivers::vga::write("Linear name   : ");
        tinyos::drivers::vga::write_line(linear != nullptr && linear->name != nullptr ? linear->name : "none");
        tinyos::drivers::vga::write("Linear size   : ");
        write_uint64(linear != nullptr ? linear->width : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(linear != nullptr ? linear->height : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(linear != nullptr ? linear->bits_per_pixel : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Linear test   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::linear_framebuffer_contract_self_test() ? "ok" : "failed");
    }

    void print_renderer_info()
    {
        const auto* state = tinyos::ui::renderer::state();
        tinyos::drivers::vga::write("Renderer ready : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Backend        : ");
        tinyos::drivers::vga::write_line(state != nullptr ? tinyos::ui::renderer::backend_name(state->backend) : "none");
        tinyos::drivers::vga::write("Surface        : ");
        tinyos::drivers::vga::write_line(state != nullptr && state->surface_name != nullptr ? state->surface_name : "none");
        tinyos::drivers::vga::write("Size           : ");
        write_uint64(state != nullptr ? state->width : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->height : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Text output    : ");
        tinyos::drivers::vga::write_line(state != nullptr && state->text_output ? "yes" : "no");
        tinyos::drivers::vga::write("Pixel output   : ");
        tinyos::drivers::vga::write_line(state != nullptr && state->pixel_output ? "yes" : "no");
        tinyos::drivers::vga::write("Bits per pixel : ");
        write_uint64(state != nullptr ? state->bits_per_pixel : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Draw calls     : ");
        write_uint64(tinyos::ui::renderer::draw_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Primitive calls: ");
        write_uint64(tinyos::ui::renderer::primitive_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pixel calls    : ");
        write_uint64(tinyos::ui::renderer::pixel_draw_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected draws : ");
        write_uint64(tinyos::ui::renderer::rejected_draw_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test      : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Primitives     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::primitive_validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Pixel contract : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::pixel_contract_validation_self_test() ? "ok" : "failed");
    }

#if !defined(TINYOS_TERMINAL_ONLY)
    void print_cursor_info()
    {
        const auto* state = tinyos::ui::cursor::state();
        tinyos::drivers::vga::write("Cursor ready  : ");
        tinyos::drivers::vga::write_line(tinyos::ui::cursor::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Visible       : ");
        tinyos::drivers::vga::write_line(state != nullptr && state->visible ? "yes" : "no");
        tinyos::drivers::vga::write("Position      : ");
        write_uint64(state != nullptr ? state->column : 0);
        tinyos::drivers::vga::write(",");
        write_uint64(state != nullptr ? state->row : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Bounds        : ");
        write_uint64(state != nullptr ? state->max_columns : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->max_rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Movements     : ");
        write_uint64(tinyos::ui::cursor::movement_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Renders       : ");
        write_uint64(tinyos::ui::cursor::render_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected ops  : ");
        write_uint64(tinyos::ui::cursor::rejected_operation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::cursor::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Render test   : ");
        tinyos::drivers::vga::write_line(tinyos::ui::cursor::render_validation_self_test() ? "ok" : "failed");
    }
#endif

    void print_terminal_info()
    {
        const auto* state = tinyos::ui::terminal::state();
        tinyos::drivers::vga::write("Terminal ready : ");
        tinyos::drivers::vga::write_line(tinyos::ui::terminal::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Backend        : ");
        tinyos::drivers::vga::write_line(state != nullptr ? tinyos::ui::renderer::backend_name(state->backend) : "none");
        tinyos::drivers::vga::write("Grid           : ");
        write_uint64(state != nullptr ? state->columns : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Status row     : ");
        write_uint64(state != nullptr ? state->status_row : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Content rows   : ");
        write_uint64(state != nullptr ? state->content_rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Status updates : ");
        write_uint64(tinyos::ui::terminal::status_update_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Line writes    : ");
        write_uint64(tinyos::ui::terminal::line_write_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Clear ops      : ");
        write_uint64(tinyos::ui::terminal::clear_operation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Panel draws    : ");
        write_uint64(tinyos::ui::terminal::panel_draw_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected ops   : ");
        write_uint64(tinyos::ui::terminal::rejected_operation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test      : ");
        tinyos::drivers::vga::write_line(tinyos::ui::terminal::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Panel test     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::terminal::panel_validation_self_test() ? "ok" : "failed");
    }

    void print_ui_event_info()
    {
        tinyos::drivers::vga::write("UI events ready : ");
        tinyos::drivers::vga::write_line(tinyos::ui::events::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Pending UI      : ");
        write_uint64(tinyos::ui::events::pending_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("UI capacity     : ");
        write_uint64(tinyos::ui::events::queue_capacity());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Driver pending  : ");
        write_uint64(tinyos::drivers::input::pending_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pushed UI       : ");
        write_uint64(tinyos::ui::events::pushed_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pumped input    : ");
        write_uint64(tinyos::ui::events::pumped_input_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Polled UI       : ");
        write_uint64(tinyos::ui::events::polled_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Dropped UI      : ");
        write_uint64(tinyos::ui::events::dropped_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test       : ");
        tinyos::drivers::vga::write_line(tinyos::ui::events::validation_self_test() ? "ok" : "failed");
    }

    void write_runtime_capabilities(uint32_t capabilities)
    {
        bool wrote_any = false;
        const uint32_t known_capabilities[] = {
            tinyos::kernel::app::runtime::CapabilityConsole,
            tinyos::kernel::app::runtime::CapabilityFileRead,
            tinyos::kernel::app::runtime::CapabilityFileWrite,
            tinyos::kernel::app::runtime::CapabilityGui,
            tinyos::kernel::app::runtime::CapabilityClock,
            tinyos::kernel::app::runtime::CapabilityNetwork
        };

        for (size_t index = 0; index < sizeof(known_capabilities) / sizeof(known_capabilities[0]); ++index)
        {
            if ((capabilities & known_capabilities[index]) == 0)
            {
                continue;
            }

            if (wrote_any)
            {
                tinyos::drivers::vga::write(",");
            }

            tinyos::drivers::vga::write(tinyos::kernel::app::runtime::capability_name(known_capabilities[index]));
            wrote_any = true;
        }

        if (!wrote_any)
        {
            tinyos::drivers::vga::write("none");
        }
    }

    void print_runtime_info()
    {
        tinyos::drivers::vga::write("Runtime manifest ready: ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::runtime::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Profiles              : ");
        write_uint64(tinyos::kernel::app::runtime::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Ready profiles        : ");
        write_uint64(tinyos::kernel::app::runtime::ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Planned profiles      : ");
        write_uint64(tinyos::kernel::app::runtime::planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-host foundation  : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::runtime::supports_self_hosted_apps() ? "ready-contract" : "missing");
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::runtime::validation_self_test() ? "ok" : "failed");

        for (size_t index = 0; index < tinyos::kernel::app::runtime::count(); ++index)
        {
            const auto* profile = tinyos::kernel::app::runtime::at(index);
            if (profile == nullptr)
            {
                continue;
            }

            tinyos::drivers::vga::write("  - ");
            tinyos::drivers::vga::write(profile->name != nullptr ? profile->name : "unnamed");
            tinyos::drivers::vga::write(" [");
            tinyos::drivers::vga::write(tinyos::kernel::app::runtime::state_name(profile->state));
            tinyos::drivers::vga::write("] ");
            tinyos::drivers::vga::write(tinyos::kernel::app::runtime::language_name(profile->language));
            tinyos::drivers::vga::write(" abi=");
            tinyos::drivers::vga::write(profile->abi != nullptr ? profile->abi : "none");
            tinyos::drivers::vga::write(" caps=");
            write_runtime_capabilities(profile->default_capabilities);
            tinyos::drivers::vga::write(" sandbox=");
            tinyos::drivers::vga::write(profile->sandboxed ? "yes" : "no");
            tinyos::drivers::vga::write(" selfhost=");
            tinyos::drivers::vga::write_line(profile->self_host_candidate ? "yes" : "no");
        }
    }

    void print_app_info()
    {
        tinyos::drivers::vga::write("App manifest ready    : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::manifest::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Profiles              : ");
        write_uint64(tinyos::kernel::app::manifest::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Ready apps            : ");
        write_uint64(tinyos::kernel::app::manifest::ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("GUI app profiles      : ");
        write_uint64(tinyos::kernel::app::manifest::gui_app_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-host candidates  : ");
        write_uint64(tinyos::kernel::app::manifest::self_host_candidate_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::manifest::validation_self_test() ? "ok" : "failed");

        for (size_t index = 0; index < tinyos::kernel::app::manifest::count(); ++index)
        {
            const auto* app = tinyos::kernel::app::manifest::at(index);
            if (app == nullptr)
            {
                continue;
            }

            tinyos::drivers::vga::write("  - ");
            tinyos::drivers::vga::write(app->name != nullptr ? app->name : "unnamed");
            tinyos::drivers::vga::write(" [");
            tinyos::drivers::vga::write(tinyos::kernel::app::manifest::state_name(app->state));
            tinyos::drivers::vga::write("] runtime=");
            tinyos::drivers::vga::write(app->runtime_name != nullptr ? app->runtime_name : "none");
            tinyos::drivers::vga::write(" caps=");
            write_runtime_capabilities(app->requested_capabilities);
            tinyos::drivers::vga::write(" launch=");
            tinyos::drivers::vga::write(tinyos::kernel::app::manifest::launchable(*app) ? "yes" : "no");
            tinyos::drivers::vga::write(" gui=");
            tinyos::drivers::vga::write(app->gui_app ? "yes" : "no");
            tinyos::drivers::vga::write(" selfhost=");
            tinyos::drivers::vga::write_line(app->self_host_candidate ? "yes" : "no");
        }
    }

    void print_launch_info()
    {
        tinyos::drivers::vga::write("Launch policy ready   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::launcher::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Checks run            : ");
        write_uint64(tinyos::kernel::app::launcher::checks_run());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Allowed checks        : ");
        write_uint64(tinyos::kernel::app::launcher::allowed_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Denied checks         : ");
        write_uint64(tinyos::kernel::app::launcher::denied_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::launcher::validation_self_test() ? "ok" : "failed");
    }

    void check_app_launch(const char* app_name)
    {
        const auto status = tinyos::kernel::app::launcher::check(app_name, tinyos::kernel::app::runtime::CapabilityNone);
        tinyos::drivers::vga::write("Launch check: ");
        tinyos::drivers::vga::write(app_name != nullptr ? app_name : "none");
        tinyos::drivers::vga::write(" -> ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::launcher::status_name(status));
    }

    void print_tapp_entry(const tinyos::kernel::app::package::Package& package)
    {
        tinyos::drivers::vga::write("  - ");
        tinyos::drivers::vga::write(package.package_name != nullptr ? package.package_name : "unnamed");
        tinyos::drivers::vga::write(" [");
        tinyos::drivers::vga::write(tinyos::kernel::app::package::state_name(package.state));
        tinyos::drivers::vga::write("] app=");
        tinyos::drivers::vga::write(package.app_name != nullptr ? package.app_name : "none");
        tinyos::drivers::vga::write(" runtime=");
        tinyos::drivers::vga::write(package.runtime_name != nullptr ? package.runtime_name : "none");
        tinyos::drivers::vga::write(" caps=");
        write_runtime_capabilities(package.required_capabilities);
        tinyos::drivers::vga::write(" launch=");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::launchable(package) ? "yes" : "no");
    }

    void print_tapp_info()
    {
        tinyos::drivers::vga::write("TAPP registry ready   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Extension             : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::extension());
        tinyos::drivers::vga::write("Packages              : ");
        write_uint64(tinyos::kernel::app::package::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Launch-ready          : ");
        write_uint64(tinyos::kernel::app::package::launch_ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Valid manifests       : ");
        write_uint64(tinyos::kernel::app::package::valid_manifest_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Planned packages      : ");
        write_uint64(tinyos::kernel::app::package::planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Signature required    : ");
        write_uint64(tinyos::kernel::app::package::signed_required_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Encryption capable    : ");
        write_uint64(tinyos::kernel::app::package::encryption_supported_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Verifier ready        : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package_verifier::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Verifier checks       : ");
        write_uint64(tinyos::kernel::app::package_verifier::checks_run());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Install-ready reports : ");
        write_uint64(tinyos::kernel::app::package_verifier::install_ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Blocked reports       : ");
        write_uint64(tinyos::kernel::app::package_verifier::blocked_count());
        tinyos::drivers::vga::put_char('\n');
    }

    void print_tapps()
    {
        print_tapp_info();
        for (size_t index = 0; index < tinyos::kernel::app::package::count(); ++index)
        {
            const auto* package = tinyos::kernel::app::package::at(index);
            if (package != nullptr)
            {
                print_tapp_entry(*package);
            }
        }
    }

    void print_tapp_detail(const char* package_name)
    {
        const auto* package = tinyos::kernel::app::package::find_package(package_name);
        if (package == nullptr)
        {
            package = tinyos::kernel::app::package::find_app(package_name);
        }

        if (package == nullptr)
        {
            tinyos::drivers::vga::write_line("TAPP package not found.");
            return;
        }

        tinyos::drivers::vga::write("Package   : ");
        tinyos::drivers::vga::write_line(package->package_name != nullptr ? package->package_name : "unnamed");
        tinyos::drivers::vga::write("State     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::state_name(package->state));
        tinyos::drivers::vga::write("Manifest  : ");
        tinyos::drivers::vga::write_line(package->manifest_path != nullptr ? package->manifest_path : "none");
        tinyos::drivers::vga::write("App       : ");
        tinyos::drivers::vga::write_line(package->app_name != nullptr ? package->app_name : "none");
        tinyos::drivers::vga::write("Runtime   : ");
        tinyos::drivers::vga::write_line(package->runtime_name != nullptr ? package->runtime_name : "none");
        tinyos::drivers::vga::write("Profile   : ");
        tinyos::drivers::vga::write_line(package->profile_name != nullptr ? package->profile_name : "none");
        tinyos::drivers::vga::write("Entry     : ");
        tinyos::drivers::vga::write_line(package->entry_path != nullptr ? package->entry_path : "none");
        tinyos::drivers::vga::write("Assets    : ");
        tinyos::drivers::vga::write_line(package->resource_root != nullptr ? package->resource_root : "none");
        tinyos::drivers::vga::write("Caps      : ");
        write_runtime_capabilities(package->required_capabilities);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Signed    : ");
        tinyos::drivers::vga::write_line(package->signed_required ? "required" : "optional");
        tinyos::drivers::vga::write("Sig state : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::signature_state_name(package->signature_state));
        tinyos::drivers::vga::write("Payload   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::payload_state_name(package->payload_state));
        tinyos::drivers::vga::write("Encrypt   : ");
        tinyos::drivers::vga::write_line(package->encryption_supported ? "supported" : "no");
        tinyos::drivers::vga::write("Trusted   : ");
        tinyos::drivers::vga::write_line(package->trusted_system_package ? "system" : "developer");
        const auto report = tinyos::kernel::app::package_verifier::verify(package);
        tinyos::drivers::vga::write("Verify    : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package_verifier::verdict_name(report.verdict));
    }

    void print_tapp_verification_report(const tinyos::kernel::app::package_verifier::Report& report)
    {
        tinyos::drivers::vga::write("TAPP verifier verdict : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package_verifier::verdict_name(report.verdict));
        if (report.package == nullptr)
        {
            return;
        }

        tinyos::drivers::vga::write("Package               : ");
        tinyos::drivers::vga::write_line(report.package->package_name != nullptr ? report.package->package_name : "unnamed");
        tinyos::drivers::vga::write("Extension             : ");
        tinyos::drivers::vga::write_line(report.extension_ok ? "ok" : "failed");
        tinyos::drivers::vga::write("Runtime               : ");
        tinyos::drivers::vga::write_line(report.runtime_ready ? "ready" : "blocked");
        tinyos::drivers::vga::write("Profile               : ");
        tinyos::drivers::vga::write_line(report.profile_ok ? "matched" : "mismatch");
        tinyos::drivers::vga::write("Trust store           : ");
        tinyos::drivers::vga::write_line(report.trust_store_ready ? "ready" : "missing");
        tinyos::drivers::vga::write("Trust algorithm       : ");
        tinyos::drivers::vga::write_line(report.trusted_algorithm ? "allowed" : "blocked");
        tinyos::drivers::vga::write("Signature             : ");
        if (report.signature_ready)
        {
            tinyos::drivers::vga::write_line("accepted");
        }
        else
        {
            tinyos::drivers::vga::write_line(report.signature_present ? "unverified" : "required");
        }
        tinyos::drivers::vga::write("Payload               : ");
        if (report.payload_ready)
        {
            tinyos::drivers::vga::write_line("accepted");
        }
        else
        {
            tinyos::drivers::vga::write_line(report.payload_hash_present ? "unverified" : "required");
        }
        tinyos::drivers::vga::write("Install gate          : ");
        tinyos::drivers::vga::write_line(report.installable ? "open" : "closed");
        tinyos::drivers::vga::write("Launch gate           : ");
        tinyos::drivers::vga::write_line(report.launchable ? "open" : "closed");
    }

    void check_tapp_package(const char* package_name)
    {
        const auto* package = tinyos::kernel::app::package::find_package(package_name);
        if (package == nullptr)
        {
            package = tinyos::kernel::app::package::find_app(package_name);
        }

        if (package == nullptr)
        {
            tinyos::drivers::vga::write_line("TAPP package not found.");
            return;
        }

        tinyos::drivers::vga::write("TAPP check: ");
        tinyos::drivers::vga::write(package->package_name != nullptr ? package->package_name : "unnamed");
        tinyos::drivers::vga::write(" -> ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::launchable(*package) ? "launch-ready" : tinyos::kernel::app::package::state_name(package->state));
        tinyos::drivers::vga::write("Profile match: ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package::profile_matches(*package) ? "yes" : "no");
        const auto report = tinyos::kernel::app::package_verifier::verify(package);
        tinyos::drivers::vga::write("Verify gate  : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::app::package_verifier::verdict_name(report.verdict));
    }

    void verify_tapp_package(const char* package_name)
    {
        const auto report = tinyos::kernel::app::package_verifier::verify_name(package_name);
        print_tapp_verification_report(report);
    }

    void print_trust_anchor_entry(const tinyos::kernel::security::trust::TrustAnchor& anchor)
    {
        tinyos::drivers::vga::write("  - ");
        tinyos::drivers::vga::write(anchor.name != nullptr ? anchor.name : "unnamed");
        tinyos::drivers::vga::write(" [");
        tinyos::drivers::vga::write(tinyos::kernel::security::trust::state_name(anchor.state));
        tinyos::drivers::vga::write("/ ");
        tinyos::drivers::vga::write(tinyos::kernel::security::trust::key_use_name(anchor.use));
        tinyos::drivers::vga::write("] alg=");
        tinyos::drivers::vga::write(anchor.algorithm != nullptr ? anchor.algorithm : "none");
        tinyos::drivers::vga::write(" app=");
        tinyos::drivers::vga::write(anchor.permits_app_packages ? "yes" : "no");
        tinyos::drivers::vga::write(" image=");
        tinyos::drivers::vga::write_line(anchor.permits_images ? "yes" : "no");
    }

    void print_trust_info()
    {
        tinyos::drivers::vga::write("Trust store ready     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::security::trust::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Anchors               : ");
        write_uint64(tinyos::kernel::security::trust::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Trusted anchors       : ");
        write_uint64(tinyos::kernel::security::trust::trusted_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Development anchors   : ");
        write_uint64(tinyos::kernel::security::trust::development_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Planned anchors       : ");
        write_uint64(tinyos::kernel::security::trust::planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("App package anchors   : ");
        write_uint64(tinyos::kernel::security::trust::app_package_anchor_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Image anchors         : ");
        write_uint64(tinyos::kernel::security::trust::image_anchor_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("RSA-SHA256 for apps   : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::security::trust::algorithm_allowed_for_apps("rsa-sha256") ? "allowed" : "blocked");
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::security::trust::validation_self_test() ? "ok" : "failed");

        for (size_t index = 0; index < tinyos::kernel::security::trust::count(); ++index)
        {
            const auto* anchor = tinyos::kernel::security::trust::at(index);
            if (anchor != nullptr)
            {
                print_trust_anchor_entry(*anchor);
            }
        }
    }

    void print_trust_anchor_detail(const char* name)
    {
        const auto* anchor = tinyos::kernel::security::trust::find(name);
        if (anchor == nullptr)
        {
            tinyos::drivers::vga::write_line("Trust anchor not found.");
            return;
        }

        tinyos::drivers::vga::write("Name        : ");
        tinyos::drivers::vga::write_line(anchor->name != nullptr ? anchor->name : "unnamed");
        tinyos::drivers::vga::write("State       : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::security::trust::state_name(anchor->state));
        tinyos::drivers::vga::write("Use         : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::security::trust::key_use_name(anchor->use));
        tinyos::drivers::vga::write("Algorithm   : ");
        tinyos::drivers::vga::write_line(anchor->algorithm != nullptr ? anchor->algorithm : "none");
        tinyos::drivers::vga::write("Fingerprint : ");
        tinyos::drivers::vga::write_line(anchor->fingerprint != nullptr ? anchor->fingerprint : "none");
        tinyos::drivers::vga::write("Source      : ");
        tinyos::drivers::vga::write_line(anchor->source != nullptr ? anchor->source : "none");
        tinyos::drivers::vga::write("App package : ");
        tinyos::drivers::vga::write_line(anchor->permits_app_packages ? "yes" : "no");
        tinyos::drivers::vga::write("Image       : ");
        tinyos::drivers::vga::write_line(anchor->permits_images ? "yes" : "no");
    }

    void print_admin_tool_summary()
    {
        tinyos::drivers::vga::write("Tools manifest ready  : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::admin::tools::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Tools total           : ");
        write_uint64(tinyos::kernel::admin::tools::count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Ready tools           : ");
        write_uint64(tinyos::kernel::admin::tools::ready_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Planned tools         : ");
        write_uint64(tinyos::kernel::admin::tools::planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("State-writing tools   : ");
        write_uint64(tinyos::kernel::admin::tools::write_tool_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("High-risk tools       : ");
        write_uint64(tinyos::kernel::admin::tools::high_risk_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::admin::tools::validation_self_test() ? "ok" : "failed");
    }

    void print_admin_tool_entry(const tinyos::kernel::admin::tools::Tool& tool)
    {
        tinyos::drivers::vga::write("  - ");
        tinyos::drivers::vga::write(tool.command != nullptr ? tool.command : "unnamed");
        tinyos::drivers::vga::write(" [");
        tinyos::drivers::vga::write(tinyos::kernel::admin::tools::state_name(tool.state));
        tinyos::drivers::vga::write("/ ");
        tinyos::drivers::vga::write(tinyos::kernel::admin::tools::category_name(tool.category));
        tinyos::drivers::vga::write("] write=");
        tinyos::drivers::vga::write(tool.writes_state ? "yes" : "no");
        tinyos::drivers::vga::write(" risk=");
        tinyos::drivers::vga::write(tool.high_risk ? "high" : "normal");
        tinyos::drivers::vga::write(" - ");
        tinyos::drivers::vga::write_line(tool.purpose != nullptr ? tool.purpose : "no purpose");
    }

    void print_admin_tools()
    {
        print_admin_tool_summary();
        for (size_t index = 0; index < tinyos::kernel::admin::tools::count(); ++index)
        {
            const auto* tool = tinyos::kernel::admin::tools::at(index);
            if (tool != nullptr)
            {
                print_admin_tool_entry(*tool);
            }
        }
    }

    void print_admin_tool_detail(const char* command)
    {
        const auto* tool = tinyos::kernel::admin::tools::find(command);
        if (tool == nullptr)
        {
            tinyos::drivers::vga::write_line("Tool not found in management manifest.");
            return;
        }

        print_admin_tool_entry(*tool);
    }

    void print_tool_risk_info()
    {
        tinyos::drivers::vga::write_line("State-writing and high-risk tools:");
        tinyos::drivers::vga::write("State-writing count: ");
        write_uint64(tinyos::kernel::admin::tools::write_tool_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("High-risk count    : ");
        write_uint64(tinyos::kernel::admin::tools::high_risk_count());
        tinyos::drivers::vga::put_char('\n');

        for (size_t index = 0; index < tinyos::kernel::admin::tools::count(); ++index)
        {
            const auto* tool = tinyos::kernel::admin::tools::at(index);
            if (tool == nullptr || (!tool->writes_state && !tool->high_risk))
            {
                continue;
            }

            print_admin_tool_entry(*tool);
        }
    }

    void print_image_step(const tinyos::kernel::provision::image::Step& step)
    {
        tinyos::drivers::vga::write("  - ");
        tinyos::drivers::vga::write(step.name != nullptr ? step.name : "unnamed");
        tinyos::drivers::vga::write(" [");
        tinyos::drivers::vga::write(tinyos::kernel::provision::image::state_name(step.state));
        tinyos::drivers::vga::write("/ ");
        tinyos::drivers::vga::write(tinyos::kernel::provision::image::phase_name(step.phase));
        tinyos::drivers::vga::write("] trust=");
        tinyos::drivers::vga::write(tinyos::kernel::provision::image::trust_level_name(step.trust_level));
        tinyos::drivers::vga::write(" key=");
        tinyos::drivers::vga::write(step.requires_key ? "yes" : "no");
        tinyos::drivers::vga::write(" remote=");
        tinyos::drivers::vga::write(step.remote_operation ? "yes" : "no");
        tinyos::drivers::vga::write(" tool=");
        tinyos::drivers::vga::write_line(step.tool != nullptr ? step.tool : "none");
    }

    void print_image_info()
    {
        tinyos::drivers::vga::write("Provision manifest    : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::provision::image::is_ready() ? "ready" : "missing");
        tinyos::drivers::vga::write("Pipeline steps        : ");
        write_uint64(tinyos::kernel::provision::image::step_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Ready contracts       : ");
        write_uint64(tinyos::kernel::provision::image::ready_contract_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Host tools planned    : ");
        write_uint64(tinyos::kernel::provision::image::host_tool_planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Kernel agents planned : ");
        write_uint64(tinyos::kernel::provision::image::kernel_planned_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Keyed steps           : ");
        write_uint64(tinyos::kernel::provision::image::key_step_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Remote steps          : ");
        write_uint64(tinyos::kernel::provision::image::remote_step_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Validation            : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::provision::image::validation_self_test() ? "ok" : "failed");

        for (size_t index = 0; index < tinyos::kernel::provision::image::step_count(); ++index)
        {
            const auto* step = tinyos::kernel::provision::image::at(index);
            if (step != nullptr)
            {
                print_image_step(*step);
            }
        }
    }

    void print_provision_info()
    {
        tinyos::drivers::vga::write_line("Provision pipeline:");
        tinyos::drivers::vga::write_line("  workspace -> config -> variants -> resources -> app -> image -> deploy -> verify -> rollback");
        tinyos::drivers::vga::write_line("Host entry point:");
        tinyos::drivers::vga::write_line("  scripts/tinyos-image.sh provision-plan|check-profile|build|manifest|keygen|sign|encrypt|deploy");
        tinyos::drivers::vga::write_line("Project workbench:");
        tinyos::drivers::vga::write_line("  planned: provisioninit, provisionconfig, provisionvariant, provisionresources, provisionui");
        tinyos::drivers::vga::write_line("Policy:");
        tinyos::drivers::vga::write_line("  isolated workspace, encryption by default, remote access opt-in");
        tinyos::drivers::vga::write_line("Kernel status:");
        print_image_info();
    }

    void print_deploy_info()
    {
        tinyos::drivers::vga::write_line("Deployment transports:");
        tinyos::drivers::vga::write_line("  ready-host: SSH/SCP/SFTP via scripts/tinyos-image.sh deploy");
        tinyos::drivers::vga::write_line("  planned-target: TinyOS provision-agent verify and rollback slots");
        tinyos::drivers::vga::write_line("  planned-link: TinyLink minimal signed transport after networking arrives");
        tinyos::drivers::vga::write("Remote steps in manifest: ");
        write_uint64(tinyos::kernel::provision::image::remote_step_count());
        tinyos::drivers::vga::put_char('\n');
    }

    void print_install_info()
    {
        tinyos::drivers::vga::write_line("Installed-system contract:");
        tinyos::drivers::vga::write_line("  current: ISO boot on i686 QEMU reference target");
        tinyos::drivers::vga::write_line("  planned: terminal installer, disk image, first-boot profile");
        tinyos::drivers::vga::write_line("  future: x86_64 and aarch64 after repeatable boot tests");
        tinyos::drivers::vga::write_line("Credential policy:");
        tinyos::drivers::vga::write_line("  credential.bootstrap=prompt, hash before storage, no plaintext secrets");
        tinyos::drivers::vga::write_line("Host entry point:");
        tinyos::drivers::vga::write_line("  scripts/tinyos-image.sh install-plan|check-install-profile");
        tinyos::drivers::vga::write_line("Target mock:");
        tinyos::drivers::vga::write_line("  installcheck validates RAMFS receipt readiness");
        tinyos::drivers::vga::write_line("  install writes /receipts/install.receipt without disk writes");
        tinyos::drivers::vga::write_line("RAMFS metadata:");
        show_file("/system/install.txt");
    }

    void print_widget_info()
    {
        const auto* state = tinyos::ui::widgets::state();
        tinyos::drivers::vga::write("Widgets ready : ");
        tinyos::drivers::vga::write_line(tinyos::ui::widgets::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Grid          : ");
        write_uint64(state != nullptr ? state->columns : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Content rows  : ");
        write_uint64(state != nullptr ? state->content_rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Label draws   : ");
        write_uint64(tinyos::ui::widgets::label_draw_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Button draws  : ");
        write_uint64(tinyos::ui::widgets::button_draw_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Handled events: ");
        write_uint64(tinyos::ui::widgets::handled_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Activations   : ");
        write_uint64(tinyos::ui::widgets::activation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected draws: ");
        write_uint64(tinyos::ui::widgets::rejected_draw_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::widgets::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Event bridge  : ");
        tinyos::drivers::vga::write_line(tinyos::ui::widgets::event_bridge_validation_self_test() ? "ok" : "failed");
    }

#if !defined(TINYOS_TERMINAL_ONLY)
    void print_window_manager_info()
    {
        const auto* state = tinyos::ui::window_manager::state();
        tinyos::drivers::vga::write("WM ready      : ");
        tinyos::drivers::vga::write_line(tinyos::ui::window_manager::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Grid          : ");
        write_uint64(state != nullptr ? state->columns : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Windows       : ");
        write_uint64(tinyos::ui::window_manager::window_count());
        tinyos::drivers::vga::put_char('\n');
        const auto* focused = tinyos::ui::window_manager::focused_window();
        tinyos::drivers::vga::write("Focused       : ");
        tinyos::drivers::vga::write_line(focused != nullptr && focused->title != nullptr ? focused->title : "none");
        tinyos::drivers::vga::write("Compositions  : ");
        write_uint64(tinyos::ui::window_manager::composition_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Focus changes : ");
        write_uint64(tinyos::ui::window_manager::focus_change_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected ops  : ");
        write_uint64(tinyos::ui::window_manager::rejected_operation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::window_manager::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Composition   : ");
        tinyos::drivers::vga::write_line(tinyos::ui::window_manager::composition_validation_self_test() ? "ok" : "failed");
        for (size_t index = 0; index < tinyos::ui::window_manager::window_count(); ++index)
        {
            const auto* window = tinyos::ui::window_manager::window_at(index);
            tinyos::drivers::vga::write("  - ");
            tinyos::drivers::vga::write(window != nullptr && window->title != nullptr ? window->title : "invalid");
            tinyos::drivers::vga::write(" role=");
            tinyos::drivers::vga::write(window != nullptr ? tinyos::ui::window_manager::role_name(window->role) : "unknown");
            tinyos::drivers::vga::write(" focused=");
            tinyos::drivers::vga::write_line(window != nullptr && window->focused ? "yes" : "no");
        }
    }

    void print_desktop_info()
    {
        const auto* state = tinyos::ui::desktop::state();
        tinyos::drivers::vga::write("Desktop ready : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::is_ready() ? "yes" : "no");
        tinyos::drivers::vga::write("Grid          : ");
        write_uint64(state != nullptr ? state->columns : 0);
        tinyos::drivers::vga::write("x");
        write_uint64(state != nullptr ? state->rows : 0);
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Launcher items: ");
        write_uint64(tinyos::ui::desktop::launcher_item_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Icons         : ");
        write_uint64(tinyos::ui::desktop::icon_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("App windows   : ");
        write_uint64(tinyos::ui::desktop::open_app_window_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pointer       : ");
        write_uint64(state != nullptr ? state->pointer_column : 0);
        tinyos::drivers::vga::write(",");
        write_uint64(state != nullptr ? state->pointer_row : 0);
        tinyos::drivers::vga::put_char('\n');
        const auto* selected = tinyos::ui::desktop::selected_launcher_item();
        tinyos::drivers::vga::write("Selected      : ");
        tinyos::drivers::vga::write_line(selected != nullptr && selected->title != nullptr ? selected->title : "none");
        tinyos::drivers::vga::write("Renders       : ");
        write_uint64(tinyos::ui::desktop::render_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Selections    : ");
        write_uint64(tinyos::ui::desktop::selection_change_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Launches      : ");
        write_uint64(tinyos::ui::desktop::launch_request_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Events        : ");
        write_uint64(tinyos::ui::desktop::handled_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Pointer events: ");
        write_uint64(tinyos::ui::desktop::pointer_event_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected ops  : ");
        write_uint64(tinyos::ui::desktop::rejected_operation_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Launcher      : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::launcher_validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Interaction   : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::interaction_validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Fullscreen    : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::fullscreen_validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Input         : ");
        tinyos::drivers::vga::write_line(tinyos::ui::desktop::input_validation_self_test() ? "ok" : "failed");
        for (size_t index = 0; index < tinyos::ui::desktop::icon_count(); ++index)
        {
            const auto* item = tinyos::ui::desktop::icon_at(index);
            tinyos::drivers::vga::write("  - ");
            tinyos::drivers::vga::write(item != nullptr && item->title != nullptr ? item->title : "invalid");
            tinyos::drivers::vga::write(" -> ");
            tinyos::drivers::vga::write(item != nullptr && item->command != nullptr ? item->command : "none");
            tinyos::drivers::vga::write(" at ");
            write_uint64(item != nullptr ? item->column : 0);
            tinyos::drivers::vga::write(",");
            write_uint64(item != nullptr ? item->row : 0);
            tinyos::drivers::vga::write_line(item != nullptr && item->selected ? " selected" : "");
        }
        for (size_t index = 0; index < tinyos::ui::desktop::app_window_count(); ++index)
        {
            const auto* window = tinyos::ui::desktop::app_window_at(index);
            if (window != nullptr && window->open)
            {
                tinyos::drivers::vga::write("  window ");
                tinyos::drivers::vga::write(window->title != nullptr ? window->title : "invalid");
                tinyos::drivers::vga::write(" -> ");
                tinyos::drivers::vga::write_line(window->command != nullptr ? window->command : "none");
            }
        }
    }

    void run_desktop_mode()
    {
        if (tinyos::kernel::device::framebuffer::has_linear_framebuffer())
        {
            if (!tinyos::ui::graphical_desktop::run_session())
            {
                tinyos::drivers::vga::write_line("Graphical desktop mode failed.");
                return;
            }

            tinyos::ui::renderer::initialize();
            tinyos::ui::terminal::initialize();
            tinyos::drivers::vga::clear();
            tinyos::drivers::vga::write_line("Returned from graphical desktop mode.");
            return;
        }

        if (!tinyos::ui::desktop::render_fullscreen())
        {
            tinyos::drivers::vga::write_line("Desktop mode failed.");
            return;
        }

        for (;;)
        {
            const char character = tinyos::drivers::keyboard::read_char();
            if (character == 27 || character == 'q' || character == 'Q')
            {
                tinyos::drivers::vga::clear();
                tinyos::ui::terminal::initialize();
                tinyos::ui::terminal::clear_content();
                tinyos::drivers::vga::write_line("Returned from desktop mode.");
                return;
            }

            tinyos::ui::events::Event event;
            event.type = tinyos::ui::events::EventType::Key;
            event.source = tinyos::ui::events::Source::Keyboard;
            event.character = character;
            event.pressed = true;
            event.column = 0;
            event.row = 0;
            event.delta_column = 0;
            event.delta_row = 0;
            event.button = 0;
            event.sequence = 0;
            tinyos::ui::desktop::handle_event(event);
        }
    }
#endif

    void print_requirements()
    {
        const auto& requirements = tinyos::kernel::platform::requirements::current();
        tinyos::drivers::vga::write("Architecture : ");
        tinyos::drivers::vga::write_line(requirements.architecture);
        tinyos::drivers::vga::write("Boot path    : ");
        tinyos::drivers::vga::write_line(requirements.boot_protocol);
        tinyos::drivers::vga::write("Firmware     : ");
        tinyos::drivers::vga::write_line(requirements.firmware_path);
        tinyos::drivers::vga::write("Minimum RAM  : ");
        write_uint64(requirements.minimum_memory_mib);
        tinyos::drivers::vga::write_line(" MiB");
        tinyos::drivers::vga::write("Recommended : ");
        write_uint64(requirements.recommended_memory_mib);
        tinyos::drivers::vga::write_line(" MiB");
        tinyos::drivers::vga::write("Display      : ");
        tinyos::drivers::vga::write_line(requirements.display);
        tinyos::drivers::vga::write("Input        : ");
        tinyos::drivers::vga::write_line(requirements.input);
        tinyos::drivers::vga::write("Timer        : ");
        tinyos::drivers::vga::write_line(requirements.timer);
        tinyos::drivers::vga::write("Interrupts   : ");
        tinyos::drivers::vga::write_line(requirements.interrupt_controller);
        tinyos::drivers::vga::write("Storage      : ");
        tinyos::drivers::vga::write_line(requirements.storage);
        tinyos::drivers::vga::write("Emulator     : ");
        tinyos::drivers::vga::write_line(requirements.emulator);
        tinyos::drivers::vga::write("Manifest     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::platform::requirements::validation_self_test() ? "ok" : "failed");
    }

    struct HelpCommand
    {
        const char* name;
        const char* summary;
        const char* usage;
        const char* run_command;
    };

    struct HelpCategory
    {
        const char* name;
        const HelpCommand* commands;
        size_t count;
    };

    const HelpCommand CoreHelp[] = {
        { "help", "open the interactive command browser", "help", nullptr },
        { "helpui", "open the interactive command browser", "helpui", nullptr },
        { "helpsearch", "search terminal command help", "helpsearch <text>", nullptr },
        { "helplist", "print the classic command list", "helplist", "helplist" },
        { "fileui", "open the terminal file browser", "fileui", "fileui" },
        { "status", "show compact terminal system dashboard", "status", "status" },
        { "sysinfo", "show TinyOS system information", "sysinfo", "sysinfo" },
        { "syscheck", "run non-destructive system health checks", "syscheck", "syscheck" },
        { "aliases", "show compatibility aliases", "aliases", "aliases" },
        { "clear", "clear the terminal", "clear", "clear" },
        { "version", "show TinyOS version", "version", "version" },
        { "echo", "print text", "echo <text>", nullptr },
        { "requirements", "show minimum machine requirements", "requirements", "requirements" },
        { "reboot", "reboot the machine", "reboot", nullptr }
    };

    const HelpCommand FileHelp[] = {
        { "pwd", "show current directory", "pwd", "pwd" },
        { "cd", "change directory", "cd <path>", nullptr },
        { "files", "list current or selected path", "files [path]", "files" },
        { "ls", "compatibility alias for files", "ls [path]", "ls" },
        { "fsmap", "show RAMFS tree", "fsmap [path]", "fsmap" },
        { "tree", "compatibility alias for fsmap", "tree [path]", "tree" },
        { "filemgr", "open the two-pane terminal file manager", "filemgr", "filemgr" },
        { "show", "print RAMFS file", "show <path>", nullptr },
        { "view", "compatibility alias for show", "view <path>", nullptr },
        { "cat", "compatibility alias for show", "cat <path>", nullptr },
        { "describe", "show RAMFS node metadata", "describe <path>", nullptr },
        { "pathcheck", "validate and inspect a VFS path", "pathcheck <path>", nullptr },
        { "fileinfo", "compatibility alias for describe", "fileinfo <path>", nullptr },
        { "mkdir", "create RAMFS directory", "mkdir <path>", nullptr },
        { "touch", "create writable RAMFS file", "touch <path>", nullptr },
        { "write", "overwrite writable RAMFS file", "write <path> <text>", nullptr },
        { "edit", "compatibility alias for write", "edit <path> <text>", nullptr },
        { "textedit", "open the interactive RAMFS text editor", "textedit <path>", nullptr },
        { "copy", "copy readable RAMFS file", "copy <source> <destination>", nullptr },
        { "cp", "compatibility alias for copy", "cp <source> <destination>", nullptr },
        { "move", "move runtime file or directory", "move <source> <destination>", nullptr },
        { "mv", "compatibility alias for move", "mv <source> <destination>", nullptr },
        { "remove", "remove runtime file or empty directory", "remove <path>", nullptr },
        { "rm", "compatibility alias for remove", "rm <path>", nullptr },
        { "chmod", "change RAMFS access mode", "chmod <mode> <path>", nullptr },
        { "fstest", "run RAMFS file operation self-test", "fstest", "fstest" },
        { "ramfsinfo", "show RAMFS state", "ramfsinfo", "ramfsinfo" },
        { "vfsinfo", "show VFS state", "vfsinfo", "vfsinfo" }
    };

    const HelpCommand DeviceHelp[] = {
        { "devices", "list registered devices", "devices", "devices" },
        { "devlist", "compatibility alias for devices", "devlist", "devlist" },
        { "device", "show one registered device", "device <name>", nullptr },
        { "blockinfo", "show RAM block device scaffold", "blockinfo", "blockinfo" },
        { "storageinfo", "show block VFS mount scaffold", "storageinfo", "storageinfo" },
        { "mount", "bind-mount block volume path", "mount <source> <target>", "mount /volumes/disk0 /mnt" },
        { "umount", "remove bind mount", "umount <target>", "umount /mnt" },
        { "fbinfo", "show framebuffer surface scaffold", "fbinfo", "fbinfo" },
        { "platforminfo", "show platform compatibility manifest", "platforminfo", "platforminfo" },
        { "pcinfo", "show PC platform initialization contract", "pcinfo", "pcinfo" },
        { "archinfo", "show architecture capability manifest", "archinfo", "archinfo" }
    };

    const HelpCommand RuntimeHelp[] = {
        { "tools", "list management tools", "tools", "tools" },
        { "toolinfo", "show management tool summary", "toolinfo", "toolinfo" },
        { "tool", "show one management tool", "tool <command>", nullptr },
        { "riskinfo", "list state-writing and high-risk tools", "riskinfo", "riskinfo" },
        { "runtimeinfo", "show language runtime manifest", "runtimeinfo", "runtimeinfo" },
        { "appinfo", "show app capability profiles", "appinfo", "appinfo" },
        { "launchinfo", "show app launch policy checks", "launchinfo", "launchinfo" },
        { "launchcheck", "dry-check an app profile", "launchcheck <app-profile>", nullptr },
        { "tappinfo", "show TAPP package registry summary", "tappinfo", "tappinfo" },
        { "tapps", "list TAPP packages", "tapps", "tapps" },
        { "tapp", "show one TAPP package", "tapp <package-or-app-name>", nullptr },
        { "tappcheck", "dry-check TAPP launch readiness", "tappcheck <package-or-app-name>", nullptr },
        { "tappverify", "verify TAPP install gate", "tappverify <package-or-app-name>", nullptr },
        { "trustinfo", "show TAPP trust store", "trustinfo", "trustinfo" },
        { "trust", "show one trust anchor", "trust <anchor-name>", nullptr },
        { "imageinfo", "show secure image pipeline", "imageinfo", "imageinfo" },
        { "provisioninfo", "show provisioning workflow", "provisioninfo", "provisioninfo" },
        { "deployinfo", "show remote deployment plan", "deployinfo", "deployinfo" },
        { "installinfo", "show installed-system contract", "installinfo", "installinfo" },
        { "installcheck", "validate installer mock preflight", "installcheck", "installcheck" },
        { "install", "write mock install receipt to RAMFS", "install", nullptr },
        { "profileinfo", "show active system profile", "profileinfo", "profileinfo" },
        { "profilecheck", "validate active system profile", "profilecheck", "profilecheck" },
        { "syscallinfo", "show syscall ABI scaffold status", "syscallinfo", "syscallinfo" },
        { "userinfo", "show user transition scaffold status", "userinfo", "userinfo" },
        { "elfinfo", "show ELF loader scaffold state", "elfinfo", "elfinfo" },
        { "modulesinfo", "show parsed boot modules", "modulesinfo", "modulesinfo" }
    };

    const HelpCommand UiHelp[] = {
        { "renderinfo", "show renderer scaffold state", "renderinfo", "renderinfo" },
        { "rendertest", "draw a renderer test label", "rendertest", "rendertest" },
        { "renderfilltest", "draw a renderer filled strip", "renderfilltest", "renderfilltest" },
        { "terminalinfo", "show terminal UI scaffold state", "terminalinfo", "terminalinfo" },
        { "terminaltest", "draw terminal UI test labels", "terminaltest", "terminaltest" },
        { "terminalclear", "clear terminal UI regions", "terminalclear", "terminalclear" },
        { "terminalpaneltest", "draw terminal UI panel", "terminalpaneltest", "terminalpaneltest" },
        { "terminalstyle", "draw styled terminal sections", "terminalstyle", "terminalstyle" },
        { "widgetinfo", "show TUI widget scaffold state", "widgetinfo", "widgetinfo" },
        { "widgettest", "draw TUI widget demo", "widgettest", "widgettest" },
        { "widgetdispatch", "dispatch queued UI events to widgets", "widgetdispatch", "widgetdispatch" },
        { "widgetactiontest", "inject and dispatch widget action", "widgetactiontest", "widgetactiontest" },
        { "uieventinfo", "show UI event queue state", "uieventinfo", "uieventinfo" },
        { "uieventpump", "move input events into UI queue", "uieventpump", "uieventpump" },
        { "uieventpeek", "read one UI event", "uieventpeek", "uieventpeek" },
        { "uieventtest", "inject a UI test key event", "uieventtest", "uieventtest" },
        { "inputinfo", "show generic input queue state", "inputinfo", "inputinfo" },
        { "inputpeek", "read one generic input event", "inputpeek", "inputpeek" },
        { "keyboardinfo", "show keyboard IRQ/input state", "keyboardinfo", "keyboardinfo" }
    #if !defined(TINYOS_TERMINAL_ONLY)
        ,{ "cursorinfo", "show cursor scaffold state", "cursorinfo", "cursorinfo" },
        { "wminfo", "show window manager scaffold state", "wminfo", "wminfo" },
        { "wmtest", "draw window manager demo", "wmtest", "wmtest" },
        { "wmfocus", "cycle window focus", "wmfocus", "wmfocus" },
        { "desktopinfo", "Alpha: show desktop shell prototype state", "desktopinfo", "desktopinfo" },
        { "desktop", "Alpha: enter fullscreen desktop mode", "desktop", nullptr },
        { "desktoptest", "Alpha: draw fullscreen desktop prototype", "desktoptest", nullptr },
        { "desktopnext", "Alpha: select next desktop launcher item", "desktopnext", nullptr },
        { "desktoplaunch", "Alpha: render selected launch request", "desktoplaunch", nullptr },
        { "desktopdispatch", "Alpha: dispatch desktop input events", "desktopdispatch", nullptr },
        { "desktopkeytest", "Alpha: test desktop keyboard flow", "desktopkeytest", nullptr },
        { "desktopmousetest", "Alpha: test desktop mouse click flow", "desktopmousetest", nullptr }
    #endif
    };

    const HelpCommand DiagnosticsHelp[] = {
        { "meminfo", "show parsed memory map summary", "meminfo", "meminfo" },
        { "frameinfo", "show physical frame allocator status", "frameinfo", "frameinfo" },
        { "heapinfo", "show kernel heap status", "heapinfo", "heapinfo" },
        { "heaptest", "run a simple heap self-test", "heaptest", "heaptest" },
        { "paginginfo", "show prepared paging structures", "paginginfo", "paginginfo" },
        { "addrspaceinfo", "show kernel address space scaffold", "addrspaceinfo", "addrspaceinfo" },
        { "irqinfo", "show IRQ diagnostic counters", "irqinfo", "irqinfo" },
        { "securityinfo", "show security scaffold status", "securityinfo", "securityinfo" },
        { "integritycheck", "run allocator integrity check", "integritycheck", "integritycheck" },
        { "schedinfo", "show scheduler scaffold state", "schedinfo", "schedinfo" },
        { "taskinfo", "show kernel task scaffold state", "taskinfo", "taskinfo" },
        { "contextinfo", "show i686 context ABI scaffold state", "contextinfo", "contextinfo" },
        { "timerinfo", "show PIT configuration and ticks", "timerinfo", "timerinfo" },
        { "uptime", "show PIT ticks", "uptime", "uptime" },
        { "yield", "record a scheduler yield", "yield", "yield" },
        { "sleeptest", "sleep for 10 PIT ticks", "sleeptest", "sleeptest" },
        { "int3", "trigger breakpoint exception", "int3", nullptr },
        { "panic", "trigger kernel panic", "panic", nullptr }
    };

    const HelpCategory HelpCategories[] = {
        { "Core", CoreHelp, sizeof(CoreHelp) / sizeof(CoreHelp[0]) },
        { "Files", FileHelp, sizeof(FileHelp) / sizeof(FileHelp[0]) },
        { "Devices", DeviceHelp, sizeof(DeviceHelp) / sizeof(DeviceHelp[0]) },
        { "Runtime", RuntimeHelp, sizeof(RuntimeHelp) / sizeof(RuntimeHelp[0]) },
        { "UI", UiHelp, sizeof(UiHelp) / sizeof(UiHelp[0]) },
        { "Diagnostics", DiagnosticsHelp, sizeof(DiagnosticsHelp) / sizeof(DiagnosticsHelp[0]) }
    };

    char ascii_lower(char value)
    {
        if (value >= 'A' && value <= 'Z')
        {
            return static_cast<char>(value - 'A' + 'a');
        }

        return value;
    }

    bool contains_text(const char* text, const char* query)
    {
        if (text == nullptr || query == nullptr || query[0] == '\0')
        {
            return false;
        }

        const size_t text_length = tinyos::core::string::length(text);
        const size_t query_length = tinyos::core::string::length(query);
        if (query_length > text_length)
        {
            return false;
        }

        for (size_t offset = 0; offset + query_length <= text_length; ++offset)
        {
            size_t matched = 0;
            while (matched < query_length && ascii_lower(text[offset + matched]) == ascii_lower(query[matched]))
            {
                ++matched;
            }

            if (matched == query_length)
            {
                return true;
            }
        }

        return false;
    }

    bool help_command_matches(const HelpCommand& command, const char* query)
    {
        return contains_text(command.name, query) ||
            contains_text(command.summary, query) ||
            contains_text(command.usage, query);
    }

    void print_help_search(const char* text)
    {
        char query[MaxPathLength];
        const char* rest = nullptr;
        if (!copy_argument(text, query, sizeof(query), rest))
        {
            tinyos::drivers::vga::write_line("Usage: helpsearch <text>");
            return;
        }

        size_t matches = 0;
        tinyos::drivers::vga::write("Help search: ");
        tinyos::drivers::vga::write_line(query);
        for (size_t category_index = 0; category_index < sizeof(HelpCategories) / sizeof(HelpCategories[0]); ++category_index)
        {
            const auto& category = HelpCategories[category_index];
            for (size_t command_index = 0; command_index < category.count; ++command_index)
            {
                const auto& command = category.commands[command_index];
                if (!help_command_matches(command, query))
                {
                    continue;
                }

                tinyos::drivers::vga::write("  ");
                tinyos::drivers::vga::write(category.name);
                tinyos::drivers::vga::write("/ ");
                tinyos::drivers::vga::write(command.name);
                tinyos::drivers::vga::write(" - ");
                tinyos::drivers::vga::write_line(command.summary);
                ++matches;
            }
        }

        tinyos::drivers::vga::write("Matches: ");
        write_uint64(matches);
        tinyos::drivers::vga::put_char('\n');
    }

    void wait_for_key()
    {
        tinyos::drivers::vga::write_line("");
        tinyos::drivers::vga::write("Press any key to continue...");
        (void)tinyos::drivers::keyboard::read_char();
    }

    void draw_help_ui(size_t category_index, size_t selected_index)
    {
        const auto& category = HelpCategories[category_index];
        constexpr size_t VisibleRows = 14;
        size_t first = 0;
        if (selected_index >= VisibleRows)
        {
            first = selected_index - VisibleRows + 1;
        }

        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write_line("TinyOS HelpUI");
        tinyos::drivers::vga::write_line("Left/Right category, Up/Down command, Enter run/details, ? details, Q/Esc exit");
        tinyos::drivers::vga::write_line("Desktop-related commands are Alpha development tools and may be incomplete.");
        tinyos::drivers::vga::write_line("");
        tinyos::drivers::vga::write("Category ");
        write_uint64(category_index + 1);
        tinyos::drivers::vga::write("/");
        write_uint64(sizeof(HelpCategories) / sizeof(HelpCategories[0]));
        tinyos::drivers::vga::write(": ");
        tinyos::drivers::vga::write_line(category.name);
        tinyos::drivers::vga::write("Command ");
        write_uint64(category.count == 0 ? 0 : selected_index + 1);
        tinyos::drivers::vga::write("/");
        write_uint64(category.count);
        tinyos::drivers::vga::write_line("");
        tinyos::drivers::vga::write_line("");

        for (size_t offset = 0; offset < VisibleRows && first + offset < category.count; ++offset)
        {
            const size_t index = first + offset;
            tinyos::drivers::vga::write(index == selected_index ? "> " : "  ");
            tinyos::drivers::vga::write(category.commands[index].name);
            tinyos::drivers::vga::write(category.commands[index].run_command != nullptr ? " * " : "   ");
            tinyos::drivers::vga::write(" - ");
            tinyos::drivers::vga::write_line(category.commands[index].summary);
        }

        tinyos::drivers::vga::write_line("");
        tinyos::drivers::vga::write_line("* Enter runs this command; commands without * show details/usage.");
    }

    void show_help_command(const HelpCommand& command)
    {
        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write("Command: ");
        tinyos::drivers::vga::write_line(command.name);
        tinyos::drivers::vga::write("Summary: ");
        tinyos::drivers::vga::write_line(command.summary);
        tinyos::drivers::vga::write("Usage  : ");
        tinyos::drivers::vga::write_line(command.usage);
        tinyos::drivers::vga::write("Run    : ");
        tinyos::drivers::vga::write_line(command.run_command != nullptr ? "Enter from HelpUI" : "manual command input required");
        wait_for_key();
    }

    void run_help_command(const HelpCommand& command)
    {
        if (command.run_command == nullptr)
        {
            show_help_command(command);
            return;
        }

        tinyos::drivers::vga::clear();
        tinyos::drivers::vga::write("Running: ");
        tinyos::drivers::vga::write_line(command.run_command);
        tinyos::drivers::vga::write_line("");
        tinyos::shell::execute(command.run_command);
        wait_for_key();
    }

    void run_help_ui()
    {
        size_t category_index = 0;
        size_t selected_index = 0;
        constexpr size_t CategoryCount = sizeof(HelpCategories) / sizeof(HelpCategories[0]);

        for (;;)
        {
            if (selected_index >= HelpCategories[category_index].count)
            {
                selected_index = 0;
            }

            draw_help_ui(category_index, selected_index);
            const char key = tinyos::drivers::keyboard::read_char();
            if (key == 'q' || key == 'Q' || key == 27)
            {
                tinyos::drivers::vga::clear();
                return;
            }
            if (key == tinyos::drivers::keyboard::KeyLeft)
            {
                category_index = category_index == 0 ? CategoryCount - 1 : category_index - 1;
                selected_index = 0;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyRight)
            {
                category_index = (category_index + 1) % CategoryCount;
                selected_index = 0;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyUp)
            {
                selected_index = selected_index == 0 ? HelpCategories[category_index].count - 1 : selected_index - 1;
                continue;
            }
            if (key == tinyos::drivers::keyboard::KeyDown)
            {
                selected_index = (selected_index + 1) % HelpCategories[category_index].count;
                continue;
            }
            if (key == '?')
            {
                show_help_command(HelpCategories[category_index].commands[selected_index]);
                continue;
            }
            if (key == '\n')
            {
                run_help_command(HelpCategories[category_index].commands[selected_index]);
            }
        }
    }

    void print_help_list()
    {
        tinyos::drivers::vga::write_line("Available commands:");
        tinyos::drivers::vga::write_line("  helpui   - interactive terminal command browser");
        tinyos::drivers::vga::write_line("  helpsearch - search command help text");
        tinyos::drivers::vga::write_line("  fileui   - interactive terminal file browser");
        tinyos::drivers::vga::write_line("  filemgr  - two-pane terminal file manager");
        tinyos::drivers::vga::write_line("  sysinfo  - show TinyOS system information");
        tinyos::drivers::vga::write_line("  helplist - print this classic command list");
        tinyos::drivers::vga::write_line("  status   - compact terminal system dashboard");
        tinyos::drivers::vga::write_line("  syscheck - run non-destructive system checks");
    #if !defined(TINYOS_TERMINAL_ONLY)
        tinyos::drivers::vga::write_line("  desktop* - Alpha desktop prototypes; development only, incomplete");
    #else
        tinyos::drivers::vga::write_line("  terminal-only - desktop/window manager commands are not linked");
    #endif
        tinyos::drivers::vga::write_line("  help     - show this help");
        tinyos::drivers::vga::write_line("  clear    - clear the screen");
        tinyos::drivers::vga::write_line("  pwd      - show current directory");
        tinyos::drivers::vga::write_line("  cd       - change current directory");
        tinyos::drivers::vga::write_line("  mkdir    - create RAMFS directory");
        tinyos::drivers::vga::write_line("  touch    - create writable RAMFS file");
        tinyos::drivers::vga::write_line("  chmod    - change RAMFS access mode");
        tinyos::drivers::vga::write_line("  files    - list TinyOS RAMFS path");
        tinyos::drivers::vga::write_line("  fsmap    - show TinyOS RAMFS tree");
        tinyos::drivers::vga::write_line("  show     - print TinyOS RAMFS file");
        tinyos::drivers::vga::write_line("  describe - show TinyOS RAMFS node details");
        tinyos::drivers::vga::write_line("  pathcheck - validate and inspect a VFS path");
        tinyos::drivers::vga::write_line("  write    - overwrite writable TinyOS RAMFS file");
        tinyos::drivers::vga::write_line("  textedit - open interactive RAMFS text editor");
        tinyos::drivers::vga::write_line("  copy/cp  - copy readable RAMFS file");
        tinyos::drivers::vga::write_line("  move/mv  - move runtime RAMFS file or directory");
        tinyos::drivers::vga::write_line("  remove/rm - remove runtime RAMFS file or empty directory");
        tinyos::drivers::vga::write_line("  fstest   - run RAMFS file operation self-test");
        tinyos::drivers::vga::write_line("  aliases  - show compatibility command aliases");
        tinyos::drivers::vga::write_line("  devices  - show TinyOS device registry");
        tinyos::drivers::vga::write_line("  device   - show one registered device");
        tinyos::drivers::vga::write_line("  blockinfo - show RAM block device scaffold");
        tinyos::drivers::vga::write_line("  storageinfo - show block VFS mount scaffold");
        tinyos::drivers::vga::write_line("  mount     - bind-mount block volume to target path");
        tinyos::drivers::vga::write_line("  umount    - remove bind mount");
        tinyos::drivers::vga::write_line("  fbinfo   - show framebuffer surface scaffold");
        tinyos::drivers::vga::write_line("  renderinfo - show renderer scaffold state");
    #if !defined(TINYOS_TERMINAL_ONLY)
        tinyos::drivers::vga::write_line("  cursorinfo - show cursor scaffold state");
    #endif
        tinyos::drivers::vga::write_line("  rendertest - draw a renderer test label");
        tinyos::drivers::vga::write_line("  renderfilltest - draw a renderer filled strip");
        tinyos::drivers::vga::write_line("  terminalinfo - show terminal UI scaffold state");
        tinyos::drivers::vga::write_line("  terminaltest - draw terminal UI test labels");
        tinyos::drivers::vga::write_line("  terminalclear - clear terminal UI regions");
        tinyos::drivers::vga::write_line("  terminalpaneltest - draw terminal UI panel");
        tinyos::drivers::vga::write_line("  terminalstyle - draw styled terminal sections");
        tinyos::drivers::vga::write_line("  widgetinfo - show TUI widget scaffold state");
        tinyos::drivers::vga::write_line("  widgettest - draw TUI widget demo");
    #if !defined(TINYOS_TERMINAL_ONLY)
        tinyos::drivers::vga::write_line("  wminfo   - show window manager scaffold state");
        tinyos::drivers::vga::write_line("  wmtest   - draw window manager demo");
        tinyos::drivers::vga::write_line("  wmfocus  - cycle window focus");
        tinyos::drivers::vga::write_line("  desktopinfo - show desktop shell prototype state");
        tinyos::drivers::vga::write_line("  desktop   - enter fullscreen desktop mode");
        tinyos::drivers::vga::write_line("  desktoptest - draw fullscreen desktop prototype");
        tinyos::drivers::vga::write_line("  desktopnext - select next desktop launcher item");
        tinyos::drivers::vga::write_line("  desktoplaunch - render selected launch request");
        tinyos::drivers::vga::write_line("  desktopdispatch - dispatch desktop input events");
        tinyos::drivers::vga::write_line("  desktopkeytest - test desktop keyboard flow");
        tinyos::drivers::vga::write_line("  desktopmousetest - test desktop mouse click flow");
    #endif
        tinyos::drivers::vga::write_line("  widgetdispatch - dispatch queued UI events to widgets");
        tinyos::drivers::vga::write_line("  widgetactiontest - inject and dispatch widget action");
        tinyos::drivers::vga::write_line("  uieventinfo - show UI event queue state");
        tinyos::drivers::vga::write_line("  uieventpump - move input events into UI queue");
        tinyos::drivers::vga::write_line("  uieventpeek - read one UI event");
        tinyos::drivers::vga::write_line("  uieventtest - inject a UI test key event");
        tinyos::drivers::vga::write_line("  tools    - list system management tools");
        tinyos::drivers::vga::write_line("  toolinfo - show management tool summary");
        tinyos::drivers::vga::write_line("  tool     - show one management tool");
        tinyos::drivers::vga::write_line("  riskinfo - list state-writing and high-risk tools");
        tinyos::drivers::vga::write_line("  runtimeinfo - show language/runtime manifest");
        tinyos::drivers::vga::write_line("  appinfo - show app capability profiles");
        tinyos::drivers::vga::write_line("  launchinfo - show app launch policy checks");
        tinyos::drivers::vga::write_line("  launchcheck - dry-check an app profile");
        tinyos::drivers::vga::write_line("  tappinfo - show TAPP package registry summary");
        tinyos::drivers::vga::write_line("  tapps    - list TAPP packages");
        tinyos::drivers::vga::write_line("  tapp     - show one TAPP package");
        tinyos::drivers::vga::write_line("  tappcheck - dry-check a TAPP package");
        tinyos::drivers::vga::write_line("  tappverify - verify a TAPP install gate");
        tinyos::drivers::vga::write_line("  trustinfo - show TAPP trust store");
        tinyos::drivers::vga::write_line("  trust    - show one trust anchor");
        tinyos::drivers::vga::write_line("  imageinfo - show secure image pipeline");
        tinyos::drivers::vga::write_line("  provisioninfo - show provisioning workflow");
        tinyos::drivers::vga::write_line("  deployinfo - show remote deployment plan");
        tinyos::drivers::vga::write_line("  installinfo - show installed-system contract");
        tinyos::drivers::vga::write_line("  installcheck - validate installer mock preflight");
        tinyos::drivers::vga::write_line("  install  - write mock install receipt to RAMFS");
        tinyos::drivers::vga::write_line("  profileinfo - show active system profile");
        tinyos::drivers::vga::write_line("  profilecheck - validate active system profile");
        tinyos::drivers::vga::write_line("  requirements - show minimum system requirements");
        tinyos::drivers::vga::write_line("  platforminfo - show platform compatibility manifest");
        tinyos::drivers::vga::write_line("  pcinfo - show PC platform initialization contract");
        tinyos::drivers::vga::write_line("  archinfo - show architecture capability manifest");
        tinyos::drivers::vga::write_line("  contextinfo - show i686 context ABI scaffold state");
        tinyos::drivers::vga::write_line("  version  - show TinyOS version");
        tinyos::drivers::vga::write_line("  echo     - print text");
        tinyos::drivers::vga::write_line("  frameinfo - show physical frame allocator status");
        tinyos::drivers::vga::write_line("  heapinfo - show kernel heap status");
        tinyos::drivers::vga::write_line("  heaptest - run a simple heap self-test");
        tinyos::drivers::vga::write_line("  irqinfo  - show IRQ diagnostic counters");
        tinyos::drivers::vga::write_line("  inputinfo - show generic input queue state");
        tinyos::drivers::vga::write_line("  inputpeek - read one generic input event");
        tinyos::drivers::vga::write_line("  keyboardinfo - show keyboard IRQ/input state");
        tinyos::drivers::vga::write_line("  integritycheck - run allocator integrity check");
        tinyos::drivers::vga::write_line("  addrspaceinfo - show kernel address space scaffold");
        tinyos::drivers::vga::write_line("  elfinfo  - show ELF loader scaffold state");
        tinyos::drivers::vga::write_line("  meminfo  - show parsed memory map summary");
        tinyos::drivers::vga::write_line("  modulesinfo - show parsed boot modules");
        tinyos::drivers::vga::write_line("  paginginfo - show prepared paging structures");
        tinyos::drivers::vga::write_line("  ramfsinfo - show RAMFS scaffold status");
        tinyos::drivers::vga::write_line("  securityinfo - show security scaffold status");
        tinyos::drivers::vga::write_line("  schedinfo - show scheduler scaffold state");
        tinyos::drivers::vga::write_line("  syscallinfo - show syscall ABI scaffold status");
        tinyos::drivers::vga::write_line("  taskinfo - show kernel task scaffold state");
        tinyos::drivers::vga::write_line("  userinfo - show user transition scaffold status");
        tinyos::drivers::vga::write_line("  vfsinfo  - show VFS scaffold status");
        tinyos::drivers::vga::write_line("  reboot   - reboot the machine");
        tinyos::drivers::vga::write_line("  timerinfo - show PIT configuration and ticks");
        tinyos::drivers::vga::write_line("  uptime   - show PIT ticks");
        tinyos::drivers::vga::write_line("  int3     - trigger breakpoint exception");
        tinyos::drivers::vga::write_line("  panic    - trigger kernel panic");
    }
}

namespace tinyos::shell
{
    [[noreturn]] void run()
    {
        char input[MaxInputLength];

        for (;;)
        {
            debug_shell_checkpoint("shell prompt");
            tinyos::drivers::vga::write("tinyos> ");
            api::get_input(input, MaxInputLength);
            debug_shell_checkpoint("shell input captured");
            execute(input);
            debug_shell_checkpoint("shell command finished");
        }
    }

    void execute(const char* input)
    {
        const char* command = core::string::skip_spaces(input);

        if (command[0] == '\0')
        {
            return;
        }

        if (core::string::compare(command, "files") == 0 || core::string::compare(command, "ls") == 0)
        {
            list_path(g_current_directory);
            return;
        }

        if (core::string::compare(command, "pwd") == 0)
        {
            drivers::vga::write_line(g_current_directory);
            return;
        }

        if (core::string::compare(command, "cd") == 0)
        {
            (void)copy_path_string(g_current_directory, sizeof(g_current_directory), "/");
            return;
        }

        if (core::string::starts_with(command, "cd "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 2, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            const auto* node = kernel::vfs::find(resolved);
            if (node == nullptr || !node->directory)
            {
                drivers::vga::write_line("Directory not found.");
                return;
            }

            if (!kernel::vfs::can_enter_directory(node))
            {
                drivers::vga::write_line("Directory not executable.");
                return;
            }

            (void)copy_path_string(g_current_directory, sizeof(g_current_directory), resolved);
            return;
        }

        if (core::string::starts_with(command, "mkdir "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 5, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::create_directory(resolved) ? "Directory created." : "Directory create failed.");
            return;
        }

        if (core::string::starts_with(command, "touch "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 5, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::create_file(resolved) ? "File created." : "File create failed.");
            return;
        }

        if (core::string::starts_with(command, "chmod "))
        {
            char mode_text[8];
            const char* path_text = nullptr;
            if (!copy_argument(command + 5, mode_text, sizeof(mode_text), path_text))
            {
                drivers::vga::write_line("Usage: chmod <mode> <path>");
                return;
            }

            uint16_t mode = 0;
            if (!parse_octal_mode(mode_text, mode))
            {
                drivers::vga::write_line("Invalid mode.");
                return;
            }

            char path[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(path_text, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::set_access_mode(resolved, mode) ? "Mode updated." : "Mode update failed.");
            return;
        }

        if (core::string::starts_with(command, "files ") || core::string::starts_with(command, "ls "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "files ") ? command + 5 : command + 2;
            if (!copy_argument(arguments, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            list_path(resolved);
            return;
        }

        if (core::string::compare(command, "fsmap") == 0 || core::string::compare(command, "tree") == 0)
        {
            print_tree(kernel::vfs::root(), 0);
            return;
        }

        if (core::string::starts_with(command, "fsmap ") || core::string::starts_with(command, "tree "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "fsmap ") ? command + 5 : command + 4;
            if (!copy_argument(arguments, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            print_tree(kernel::vfs::find(resolved), 0);
            return;
        }

        if (core::string::starts_with(command, "show ") || core::string::starts_with(command, "view ") || core::string::starts_with(command, "cat "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "cat ") ? command + 3 : command + 4;
            if (!copy_argument(arguments, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            show_file(resolved);
            return;
        }

        if (core::string::starts_with(command, "describe ") || core::string::starts_with(command, "fileinfo "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "describe ") ? command + 8 : command + 8;
            if (!copy_argument(arguments, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            show_file_info(resolved);
            return;
        }

        if (core::string::starts_with(command, "pathcheck "))
        {
            print_path_check(command + 9);
            return;
        }

        if (core::string::compare(command, "pathcheck") == 0)
        {
            drivers::vga::write_line("Usage: pathcheck <path>");
            return;
        }

        if (core::string::starts_with(command, "textedit "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 8, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Usage: textedit <path>");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            run_text_editor(resolved);
            return;
        }

        if (core::string::compare(command, "textedit") == 0)
        {
            drivers::vga::write_line("Usage: textedit <path>");
            return;
        }

        if (core::string::starts_with(command, "write ") || core::string::starts_with(command, "edit "))
        {
            char path[MaxPathLength];
            const char* text = nullptr;
            const char* arguments = core::string::starts_with(command, "write ") ? command + 5 : command + 4;
            if (!copy_argument(arguments, path, sizeof(path), text) || text == nullptr)
            {
                drivers::vga::write_line("Invalid write command.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            edit_file(resolved, text);
            return;
        }

        if (core::string::starts_with(command, "copy ") || core::string::starts_with(command, "cp "))
        {
            char source[MaxPathLength];
            char destination[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "copy ") ? command + 5 : command + 2;
            if (!copy_argument(arguments, source, sizeof(source), rest) || !copy_argument(rest, destination, sizeof(destination), rest))
            {
                drivers::vga::write_line("Usage: copy <source> <destination>");
                return;
            }

            char resolved_source[MaxPathLength];
            char resolved_destination[MaxPathLength];
            if (!resolve_shell_path(source, resolved_source, sizeof(resolved_source)) || !resolve_shell_path(destination, resolved_destination, sizeof(resolved_destination)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::copy_file(resolved_source, resolved_destination) ? "File copied." : "Copy failed.");
            return;
        }

        if (core::string::starts_with(command, "move ") || core::string::starts_with(command, "mv "))
        {
            char source[MaxPathLength];
            char destination[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "move ") ? command + 5 : command + 2;
            if (!copy_argument(arguments, source, sizeof(source), rest) || !copy_argument(rest, destination, sizeof(destination), rest))
            {
                drivers::vga::write_line("Usage: move <source> <destination>");
                return;
            }

            char resolved_source[MaxPathLength];
            char resolved_destination[MaxPathLength];
            if (!resolve_shell_path(source, resolved_source, sizeof(resolved_source)) || !resolve_shell_path(destination, resolved_destination, sizeof(resolved_destination)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::move(resolved_source, resolved_destination) ? "Path moved." : "Move failed.");
            return;
        }

        if (core::string::starts_with(command, "remove ") || core::string::starts_with(command, "rm "))
        {
            char path[MaxPathLength];
            const char* rest = nullptr;
            const char* arguments = core::string::starts_with(command, "remove ") ? command + 7 : command + 2;
            if (!copy_argument(arguments, path, sizeof(path), rest))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            char resolved[MaxPathLength];
            if (!resolve_shell_path(path, resolved, sizeof(resolved)))
            {
                drivers::vga::write_line("Invalid path.");
                return;
            }

            drivers::vga::write_line(kernel::vfs::remove(resolved) ? "Path removed." : "Remove failed.");
            return;
        }

        if (core::string::compare(command, "fstest") == 0)
        {
            drivers::vga::write_line(run_fs_self_test() ? "Filesystem self-test passed." : "Filesystem self-test failed.");
            return;
        }

        if (core::string::compare(command, "aliases") == 0)
        {
            drivers::vga::write_line("Compatibility aliases:");
            drivers::vga::write_line("  ls       -> files");
            drivers::vga::write_line("  tree     -> fsmap");
            drivers::vga::write_line("  cat/view -> show");
            drivers::vga::write_line("  fileinfo -> describe");
            drivers::vga::write_line("  pwd/cd   -> shell directory navigation");
            drivers::vga::write_line("  mkdir    -> create RAMFS directory");
            drivers::vga::write_line("  chmod    -> change RAMFS access mode");
            drivers::vga::write_line("  edit     -> write");
            drivers::vga::write_line("  cp       -> copy");
            drivers::vga::write_line("  mv       -> move");
            drivers::vga::write_line("  rm       -> remove");
            drivers::vga::write_line("  devlist  -> devices");
            return;
        }

        if (core::string::compare(command, "devices") == 0 || core::string::compare(command, "devlist") == 0)
        {
            print_device_registry();
            return;
        }

        if (core::string::starts_with(command, "device "))
        {
            char name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 6, name, sizeof(name), rest))
            {
                drivers::vga::write_line("Invalid device name.");
                return;
            }

            print_device_detail(name);
            return;
        }

        if (core::string::compare(command, "blockinfo") == 0)
        {
            print_block_info();
            return;
        }

        if (handle_mount_command(command))
        {
            return;
        }

        if (core::string::compare(command, "storageinfo") == 0)
        {
            print_storage_info();
            return;
        }

        if (core::string::compare(command, "fbinfo") == 0)
        {
            print_framebuffer_info();
            return;
        }

        if (core::string::compare(command, "renderinfo") == 0)
        {
            print_renderer_info();
            return;
        }

    #if !defined(TINYOS_TERMINAL_ONLY)
        if (core::string::compare(command, "cursorinfo") == 0)
        {
            print_cursor_info();
            return;
        }
    #endif

        if (core::string::compare(command, "rendertest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::renderer::draw_text(0, 24, "TinyOS renderer test", 0x0A) ? "Renderer test drawn." : "Renderer test failed.");
            return;
        }

        if (core::string::compare(command, "renderfilltest") == 0)
        {
            const bool filled = tinyos::ui::renderer::fill_rect(0, 23, 28, 1, ' ', 0x1E);
            const bool labeled = tinyos::ui::renderer::draw_text(0, 23, "TinyOS fill primitive", 0x1E);
            drivers::vga::write_line(filled && labeled ? "Renderer fill test drawn." : "Renderer fill test failed.");
            return;
        }

        if (core::string::compare(command, "terminalinfo") == 0)
        {
            print_terminal_info();
            return;
        }

        if (core::string::compare(command, "terminaltest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::terminal::render_self_test_label() ? "Terminal UI test drawn." : "Terminal UI test failed.");
            return;
        }

        if (core::string::compare(command, "terminalclear") == 0)
        {
            const bool status_cleared = tinyos::ui::terminal::clear_status();
            const bool content_cleared = tinyos::ui::terminal::clear_content();
            drivers::vga::write_line(status_cleared && content_cleared ? "Terminal UI regions cleared." : "Terminal UI clear failed.");
            return;
        }

        if (core::string::compare(command, "terminalpaneltest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::terminal::render_panel_self_test() ? "Terminal panel test drawn." : "Terminal panel test failed.");
            return;
        }

        if (core::string::compare(command, "terminalstyle") == 0)
        {
            drivers::vga::write_line(tinyos::ui::terminal::render_color_demo() ? "Terminal style demo drawn." : "Terminal style demo failed.");
            return;
        }

        if (core::string::compare(command, "widgetinfo") == 0)
        {
            print_widget_info();
            return;
        }

        if (core::string::compare(command, "widgettest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::widgets::render_demo() ? "TUI widget demo drawn." : "TUI widget demo failed.");
            return;
        }

    #if !defined(TINYOS_TERMINAL_ONLY)
        if (core::string::compare(command, "wminfo") == 0)
        {
            print_window_manager_info();
            return;
        }

        if (core::string::compare(command, "wmtest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::window_manager::render_demo() ? "Window manager demo drawn." : "Window manager demo failed.");
            return;
        }

        if (core::string::compare(command, "wmfocus") == 0)
        {
            const bool focused = tinyos::ui::window_manager::focus_next();
            const bool drawn = focused && tinyos::ui::window_manager::compose();
            drivers::vga::write_line(focused && drawn ? "Window focus changed." : "Window focus failed.");
            return;
        }

        if (core::string::compare(command, "desktopinfo") == 0)
        {
            print_desktop_info();
            return;
        }

        if (core::string::compare(command, "desktop") == 0)
        {
            run_desktop_mode();
            return;
        }

        if (core::string::compare(command, "desktoptest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::desktop::render_demo() ? "Fullscreen desktop demo drawn." : "Fullscreen desktop demo failed.");
            return;
        }

        if (core::string::compare(command, "desktopnext") == 0)
        {
            const bool selected = tinyos::ui::desktop::select_next();
            const bool drawn = selected && tinyos::ui::desktop::render_fullscreen();
            drivers::vga::write_line(selected && drawn ? "Desktop launcher selection changed." : "Desktop launcher selection failed.");
            return;
        }

        if (core::string::compare(command, "desktoplaunch") == 0)
        {
            drivers::vga::write_line(tinyos::ui::desktop::launch_selected() ? "Desktop launch request rendered." : "Desktop launch request failed.");
            return;
        }

        if (core::string::compare(command, "desktopdispatch") == 0)
        {
            tinyos::ui::events::pump_from_input(tinyos::ui::events::queue_capacity());
            drivers::vga::write("Desktop events dispatched: ");
            write_uint64(tinyos::ui::desktop::dispatch_events(tinyos::ui::events::queue_capacity()));
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "desktopkeytest") == 0)
        {
            const bool tab_queued = tinyos::ui::events::push_key_event('\t', true);
            const size_t tab_dispatched = tinyos::ui::desktop::dispatch_events(1);
            const bool enter_queued = tinyos::ui::events::push_key_event('\n', true);
            const size_t enter_dispatched = tinyos::ui::desktop::dispatch_events(1);
            drivers::vga::write_line(tab_queued && tab_dispatched == 1 && enter_queued && enter_dispatched == 1 ? "Desktop keyboard flow dispatched." : "Desktop keyboard flow failed.");
            return;
        }

        if (core::string::compare(command, "desktopmousetest") == 0)
        {
            const tinyos::ui::desktop::DesktopIcon* icon = nullptr;
            for (size_t index = 0; index < tinyos::ui::desktop::icon_count(); ++index)
            {
                const auto* current = tinyos::ui::desktop::icon_at(index);
                if (current != nullptr && current->selected)
                {
                    icon = current;
                    break;
                }
            }

            const bool queued = icon != nullptr && tinyos::ui::events::push_mouse_button_event(icon->column + 1, icon->row + 1, 1, true);
            const size_t dispatched = queued ? tinyos::ui::desktop::dispatch_events(1) : 0;
            drivers::vga::write_line(queued && dispatched == 1 ? "Desktop mouse click dispatched." : "Desktop mouse click failed.");
            return;
        }
#endif

        if (core::string::compare(command, "widgetdispatch") == 0)
        {
            tinyos::ui::events::pump_from_input(tinyos::ui::events::queue_capacity());
            drivers::vga::write("Widget events dispatched: ");
            write_uint64(tinyos::ui::widgets::dispatch_events(tinyos::ui::events::queue_capacity()));
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "widgetactiontest") == 0)
        {
            const bool queued = tinyos::ui::events::push_key_event(' ', true);
            const size_t dispatched = tinyos::ui::widgets::dispatch_events(1);
            drivers::vga::write_line(queued && dispatched == 1 ? "Widget action dispatched." : "Widget action failed.");
            return;
        }

        if (core::string::compare(command, "uieventinfo") == 0)
        {
            print_ui_event_info();
            return;
        }

        if (core::string::compare(command, "uieventpump") == 0)
        {
            drivers::vga::write("Pumped UI events: ");
            write_uint64(tinyos::ui::events::pump_from_input(tinyos::ui::events::queue_capacity()));
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "uieventpeek") == 0)
        {
            tinyos::ui::events::Event event;
            event.type = tinyos::ui::events::EventType::None;
            event.source = tinyos::ui::events::Source::None;
            event.character = 0;
            event.pressed = false;
            event.column = 0;
            event.row = 0;
            event.delta_column = 0;
            event.delta_row = 0;
            event.button = 0;
            event.sequence = 0;

            if (!tinyos::ui::events::poll_event(event))
            {
                drivers::vga::write_line("No pending UI events.");
                return;
            }

            drivers::vga::write("UI event #");
            write_uint64(event.sequence);
            drivers::vga::write(" ");
            drivers::vga::write(tinyos::ui::events::event_type_name(event.type));
            drivers::vga::write(" from ");
            drivers::vga::write(tinyos::ui::events::source_name(event.source));
            if (event.type == tinyos::ui::events::EventType::Key)
            {
                drivers::vga::write(" '");
                drivers::vga::put_char(event.character);
                drivers::vga::write(event.pressed ? "' pressed" : "' released");
            }
            if (event.type == tinyos::ui::events::EventType::Pointer || event.type == tinyos::ui::events::EventType::MouseButton)
            {
                drivers::vga::write(" at ");
                write_uint64(event.column);
                drivers::vga::write(",");
                write_uint64(event.row);
                if (event.type == tinyos::ui::events::EventType::MouseButton)
                {
                    drivers::vga::write(" button=");
                    write_uint64(event.button);
                    drivers::vga::write(event.pressed ? " pressed" : " released");
                }
            }
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "uieventtest") == 0)
        {
            drivers::vga::write_line(tinyos::ui::events::push_key_event('T', true) ? "UI test event queued." : "UI test event failed.");
            return;
        }

        if (core::string::compare(command, "toolinfo") == 0)
        {
            print_admin_tool_summary();
            return;
        }

        if (core::string::compare(command, "riskinfo") == 0)
        {
            print_tool_risk_info();
            return;
        }

        if (core::string::compare(command, "tools") == 0)
        {
            print_admin_tools();
            return;
        }

        if (core::string::starts_with(command, "tool "))
        {
            char tool_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 4, tool_name, sizeof(tool_name), rest))
            {
                drivers::vga::write_line("Invalid tool name.");
                return;
            }

            print_admin_tool_detail(tool_name);
            return;
        }

        if (core::string::compare(command, "tool") == 0)
        {
            drivers::vga::write_line("Usage: tool <command>");
            return;
        }

        if (core::string::compare(command, "runtimeinfo") == 0)
        {
            print_runtime_info();
            return;
        }

        if (core::string::compare(command, "appinfo") == 0)
        {
            print_app_info();
            return;
        }

        if (core::string::compare(command, "launchinfo") == 0)
        {
            print_launch_info();
            return;
        }

        if (core::string::starts_with(command, "launchcheck "))
        {
            char app_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 12, app_name, sizeof(app_name), rest))
            {
                drivers::vga::write_line("Invalid app profile name.");
                return;
            }

            check_app_launch(app_name);
            return;
        }

        if (core::string::compare(command, "launchcheck") == 0)
        {
            drivers::vga::write_line("Usage: launchcheck <app-profile>");
            return;
        }

        if (core::string::compare(command, "tappinfo") == 0)
        {
            print_tapp_info();
            return;
        }

        if (core::string::compare(command, "tapps") == 0)
        {
            print_tapps();
            return;
        }

        if (core::string::starts_with(command, "tapp "))
        {
            char package_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 5, package_name, sizeof(package_name), rest))
            {
                drivers::vga::write_line("Invalid TAPP package name.");
                return;
            }

            print_tapp_detail(package_name);
            return;
        }

        if (core::string::compare(command, "tapp") == 0)
        {
            drivers::vga::write_line("Usage: tapp <package-or-app-name>");
            return;
        }

        if (core::string::starts_with(command, "tappcheck "))
        {
            char package_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 10, package_name, sizeof(package_name), rest))
            {
                drivers::vga::write_line("Invalid TAPP package name.");
                return;
            }

            check_tapp_package(package_name);
            return;
        }

        if (core::string::compare(command, "tappcheck") == 0)
        {
            drivers::vga::write_line("Usage: tappcheck <package-or-app-name>");
            return;
        }

        if (core::string::starts_with(command, "tappverify "))
        {
            char package_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 11, package_name, sizeof(package_name), rest))
            {
                drivers::vga::write_line("Invalid TAPP package name.");
                return;
            }

            verify_tapp_package(package_name);
            return;
        }

        if (core::string::compare(command, "tappverify") == 0)
        {
            drivers::vga::write_line("Usage: tappverify <package-or-app-name>");
            return;
        }

        if (core::string::compare(command, "trustinfo") == 0)
        {
            print_trust_info();
            return;
        }

        if (core::string::starts_with(command, "trust "))
        {
            char anchor_name[MaxPathLength];
            const char* rest = nullptr;
            if (!copy_argument(command + 6, anchor_name, sizeof(anchor_name), rest))
            {
                drivers::vga::write_line("Invalid trust anchor name.");
                return;
            }

            print_trust_anchor_detail(anchor_name);
            return;
        }

        if (core::string::compare(command, "trust") == 0)
        {
            drivers::vga::write_line("Usage: trust <anchor-name>");
            return;
        }

        if (core::string::compare(command, "imageinfo") == 0)
        {
            print_image_info();
            return;
        }

        if (core::string::compare(command, "provisioninfo") == 0)
        {
            print_provision_info();
            return;
        }

        if (core::string::compare(command, "deployinfo") == 0)
        {
            print_deploy_info();
            return;
        }

        if (core::string::compare(command, "installinfo") == 0)
        {
            print_install_info();
            return;
        }

        if (core::string::compare(command, "installcheck") == 0)
        {
            print_install_check();
            return;
        }

        if (core::string::compare(command, "install") == 0)
        {
            run_install_mock();
            return;
        }

        if (core::string::compare(command, "profileinfo") == 0)
        {
            print_profile_info();
            return;
        }

        if (core::string::compare(command, "profilecheck") == 0)
        {
            print_profile_check();
            return;
        }

        if (core::string::compare(command, "requirements") == 0)
        {
            print_requirements();
            return;
        }

        if (core::string::compare(command, "elfinfo") == 0)
        {
            drivers::vga::write("ELF loader ready : ");
            drivers::vga::write_line(kernel::elf::loader::is_ready() ? "yes" : "no");
            drivers::vga::write("Modules scanned   : ");
            write_uint64(kernel::elf::loader::scanned_module_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Valid ELF images  : ");
            write_uint64(kernel::elf::loader::valid_image_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Raw modules       : ");
            write_uint64(kernel::elf::loader::raw_module_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Invalid ELF images: ");
            write_uint64(kernel::elf::loader::invalid_image_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Validation        : ");
            drivers::vga::write_line(kernel::elf::loader::validation_passed() ? "ok" : "failed");
            drivers::vga::write("Self-test         : ");
            drivers::vga::write_line(kernel::elf::loader::validation_self_test() ? "ok" : "failed");

            for (size_t index = 0; index < kernel::elf::loader::scanned_module_count(); ++index)
            {
                const auto* image = kernel::elf::loader::image_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(image != nullptr && image->name != nullptr ? image->name : "unnamed");
                drivers::vga::write(" [");
                drivers::vga::write(image != nullptr ? kernel::elf::loader::status_name(image->status) : "missing");
                drivers::vga::write("] ");
                drivers::vga::write("entry ");
                write_uint64(image != nullptr ? image->entry_point : 0);
                drivers::vga::write(" phoff ");
                write_uint64(image != nullptr ? image->program_header_offset : 0);
                drivers::vga::write(" phnum ");
                write_uint64(image != nullptr ? image->program_header_count : 0);
                drivers::vga::put_char('\n');
            }
            return;
        }

        if (core::string::compare(command, "securityinfo") == 0)
        {
            drivers::vga::write("Integrity ready: ");
            drivers::vga::write_line(kernel::security::integrity::is_ready() ? "yes" : "no");
            drivers::vga::write("Kernel warnings: ");
            write_uint64(kernel::klog::warning_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Memory warnings: ");
            write_uint64(kernel::klog::warning_count(kernel::klog::WarningCategory::Memory));
            drivers::vga::put_char('\n');
            drivers::vga::write("Driver warnings: ");
            write_uint64(kernel::klog::warning_count(kernel::klog::WarningCategory::Driver));
            drivers::vga::put_char('\n');
            drivers::vga::write("Generic warnings: ");
            write_uint64(kernel::klog::warning_count(kernel::klog::WarningCategory::Generic));
            drivers::vga::put_char('\n');
            drivers::vga::write("Kernel errors  : ");
            write_uint64(kernel::klog::error_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Checks run     : ");
            write_uint64(kernel::security::integrity::checks_run());
            drivers::vga::put_char('\n');
            drivers::vga::write("Allocator state: ");
            drivers::vga::write_line(kernel::security::integrity::allocator_state_valid() ? "ok" : "failed");
            drivers::vga::write("Boot modules  : ");
            drivers::vga::write_line(kernel::security::integrity::boot_modules_valid() ? "ok" : "failed");
            drivers::vga::write_line("Roadmap: docs/security-roadmap.md");
            return;
        }

        if (core::string::compare(command, "sysinfo") == 0)
        {
            print_system_information();
            return;
        }

        if (core::string::compare(command, "syscallinfo") == 0)
        {
            drivers::vga::write("Syscall ready: ");
            drivers::vga::write_line(kernel::syscall::is_ready() ? "yes" : "no");
            drivers::vga::write("Syscall count: ");
            write_uint64(kernel::syscall::count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Definitions : ");
            write_uint64(kernel::syscall::definition_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Implemented : ");
            write_uint64(kernel::syscall::implemented_definition_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Max buffer   : ");
            write_uint64(kernel::syscall::max_user_buffer_bytes());
            drivers::vga::put_char('\n');
            const auto& policy = kernel::syscall::boundary_policy();
            drivers::vga::write("Max args     : ");
            write_uint64(policy.max_argument_count);
            drivers::vga::put_char('\n');
            drivers::vga::write("Null guard   : ");
            write_uint64(policy.null_guard_bytes);
            drivers::vga::write_line(" bytes");
            drivers::vga::write("Unknown nums : ");
            drivers::vga::write_line(policy.reject_unknown_numbers ? "reject" : "allow");
            drivers::vga::write("Access mode  : ");
            drivers::vga::write_line(policy.require_explicit_buffer_access ? "explicit" : "implicit");
            const auto& filter = kernel::syscall::filter_policy();
            drivers::vga::write("Filter impl  : ");
            drivers::vga::write_line(filter.deny_unimplemented ? "deny-unimplemented" : "allow-unimplemented");
            drivers::vga::write("Filter count : ");
            drivers::vga::write_line(filter.count_filtered_as_rejected ? "rejected" : "ignored");
            const auto& resource = kernel::syscall::resource_policy();
            drivers::vga::write("Reject limit : ");
            write_uint64(resource.max_rejected_calls_before_throttle);
            drivers::vga::put_char('\n');
            drivers::vga::write("Throttle     : ");
            drivers::vga::write_line(kernel::syscall::throttle_active() ? "active" : "inactive");
            drivers::vga::write("Validation   : ");
            drivers::vga::write_line(kernel::syscall::validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Policy test  : ");
            drivers::vga::write_line(kernel::syscall::boundary_policy_validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Defs test    : ");
            drivers::vga::write_line(kernel::syscall::definition_validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Filter test  : ");
            drivers::vga::write_line(kernel::syscall::filter_policy_validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Limit test   : ");
            drivers::vga::write_line(kernel::syscall::resource_policy_validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Sched test   : ");
            drivers::vga::write_line(kernel::syscall::scheduling_validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Bad buffers  : ");
            write_uint64(kernel::syscall::validation_failure_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Rejected calls: ");
            write_uint64(kernel::syscall::rejected_call_count());
            drivers::vga::put_char('\n');
            for (size_t index = 0; index < kernel::syscall::definition_count(); ++index)
            {
                const auto* definition = kernel::syscall::definition_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(definition != nullptr ? definition->name : "invalid");
                drivers::vga::write(" args=");
                write_uint64(definition != nullptr ? definition->argument_count : 0);
                drivers::vga::write(" impl=");
                drivers::vga::write_line(definition != nullptr && definition->implemented ? "yes" : "no");
            }
            return;
        }

        if (core::string::compare(command, "schedinfo") == 0)
        {
            drivers::vga::write("Scheduler ready : ");
            drivers::vga::write_line(kernel::sched::is_ready() ? "yes" : "no");
            drivers::vga::write("Scheduler ticks : ");
            write_uint64(kernel::sched::tick_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Runnable tasks  : ");
            write_uint64(kernel::sched::runnable_task_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Blocked tasks   : ");
            write_uint64(kernel::sched::blocked_task_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Idle tasks      : ");
            write_uint64(kernel::sched::idle_task_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Yield calls     : ");
            write_uint64(kernel::sched::yield_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Sleep calls     : ");
            write_uint64(kernel::sched::sleep_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Wake events     : ");
            write_uint64(kernel::sched::wake_event_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Context switches: ");
            write_uint64(kernel::sched::context_switch_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Dispatch picks  : ");
            write_uint64(kernel::sched::dispatch_decision_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Time slice ticks: ");
            write_uint64(kernel::sched::time_slice_ticks());
            drivers::vga::put_char('\n');
            drivers::vga::write("Last selected  : ");
            write_uint64(kernel::sched::last_selected_task_id());
            drivers::vga::put_char('\n');
            drivers::vga::write("Contexts ready  : ");
            write_uint64(kernel::task::prepared_context_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Round-robin     : ");
            drivers::vga::write_line(kernel::sched::round_robin_ready() ? "ready" : "blocked");
            drivers::vga::write("Sleep/wake      : ");
            drivers::vga::write_line(kernel::sched::sleep_wake_ready() ? "ready" : "blocked");
            drivers::vga::write("Preemption      : ");
            drivers::vga::write_line(kernel::sched::preemption_enabled() ? "enabled" : "not yet");
            drivers::vga::write("Guard pages     : ");
            drivers::vga::write_line(kernel::task::guard_pages_ready() ? "installed" : "missing");
            drivers::vga::write("Watchdog warns  : ");
            write_uint64(kernel::sched::watchdog_warning_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Watchdog limit  : ");
            write_uint64(kernel::sched::watchdog_threshold_ticks());
            drivers::vga::put_char('\n');
            drivers::vga::write("Ticks since sw  : ");
            write_uint64(kernel::sched::ticks_since_last_switch());
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "contextinfo") == 0)
        {
            drivers::vga::write("Context ABI      : ");
            drivers::vga::write_line(arch::context::abi_name());
            drivers::vga::write("Context size     : ");
            write_uint64(arch::context::context_size());
            drivers::vga::put_char('\n');
            drivers::vga::write("Stack alignment  : ");
            write_uint64(arch::context::RequiredStackAlignment);
            drivers::vga::put_char('\n');
            drivers::vga::write("Switch available : ");
            drivers::vga::write_line(arch::context::context_switch_available() ? "yes" : "not yet");
            drivers::vga::write("Prepared contexts: ");
            write_uint64(kernel::task::prepared_context_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Context bytes    : ");
            write_uint64(kernel::task::context_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("All ready        : ");
            drivers::vga::write_line(kernel::task::contexts_ready() ? "yes" : "no");
            return;
        }

        if (core::string::compare(command, "taskinfo") == 0)
        {
            drivers::vga::write("Task count: ");
            write_uint64(kernel::task::task_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Owned stacks: ");
            write_uint64(kernel::task::owned_kernel_stack_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Stack bytes : ");
            write_uint64(kernel::task::kernel_stack_bytes());
            drivers::vga::put_char('\n');
            const auto* current = kernel::sched::current_task();
            drivers::vga::write("Current   : ");
            drivers::vga::write_line(current != nullptr ? current->name : "none");

            for (size_t index = 0; index < kernel::task::task_count(); ++index)
            {
                const auto* task = kernel::task::task_at(index);
                if (task == nullptr)
                {
                    continue;
                }

                drivers::vga::write("  #");
                write_uint64(task->id);
                drivers::vga::write(" ");
                drivers::vga::write(task->name != nullptr ? task->name : "unnamed");
                drivers::vga::write(" state=");
                drivers::vga::write(kernel::task::state_name(task->state));
                drivers::vga::write(" ticks=");
                write_uint64(task->runtime_ticks);
                drivers::vga::write(" oncpu=");
                write_uint64(task->ticks_on_cpu);
                drivers::vga::write(" wake=");
                write_uint64(task->wake_tick);
                drivers::vga::write(" stack=");
                write_uint64(task->kernel_stack_size);
                drivers::vga::write(" ctx=");
                drivers::vga::write(task->context_ready ? "ready" : "missing");
                drivers::vga::put_char('\n');
            }
            return;
        }

        if (core::string::compare(command, "integritycheck") == 0)
        {
            drivers::vga::write("Allocator integrity: ");
            drivers::vga::write_line(kernel::security::integrity::allocator_state_valid() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "userinfo") == 0)
        {
            drivers::vga::write("User transition ready: ");
            drivers::vga::write_line(kernel::user::transition::is_ready() ? "yes" : "no");
            drivers::vga::write("Syscall vector      : ");
            write_uint64(kernel::user::transition::syscall_vector());
            drivers::vga::put_char('\n');
            drivers::vga::write("User code selector  : ");
            write_uint64(kernel::user::transition::user_code_selector());
            drivers::vga::put_char('\n');
            drivers::vga::write("User data selector  : ");
            write_uint64(kernel::user::transition::user_data_selector());
            drivers::vga::put_char('\n');
            drivers::vga::write("Stack alignment     : ");
            write_uint64(kernel::user::transition::user_stack_alignment());
            drivers::vga::put_char('\n');
            drivers::vga::write("Init process       : ");
            drivers::vga::write_line(kernel::user::transition::init_process_name());
            drivers::vga::write("Init entry         : ");
            drivers::vga::write_line(kernel::user::transition::init_entry_path());
            drivers::vga::write("Init stack top     : ");
            write_uint64(kernel::user::transition::init_user_stack_top());
            drivers::vga::put_char('\n');
            drivers::vga::write("Init stack bytes   : ");
            write_uint64(kernel::user::transition::init_user_stack_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("Init launch        : ");
            drivers::vga::write_line(kernel::user::transition::init_launch_supported() ? "supported" : "contract-only");
            drivers::vga::write("Init contract      : ");
            drivers::vga::write_line(kernel::user::transition::validation_self_test() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "addrspaceinfo") == 0)
        {
            drivers::vga::write("Address space ready: ");
            drivers::vga::write_line(kernel::memory::address_space::is_ready() ? "yes" : "no");
            drivers::vga::write("Region count      : ");
            write_uint64(kernel::memory::address_space::region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Kernel sections   : ");
            write_uint64(kernel::memory::address_space::kernel_section_region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Boot modules      : ");
            write_uint64(kernel::memory::address_space::boot_module_region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Rejected regions  : ");
            write_uint64(kernel::memory::address_space::rejected_region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Mapped total MiB : ");
            write_uint64(kernel::memory::address_space::total_mapped_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            drivers::vga::write("Self-test        : ");
            drivers::vga::write_line(kernel::memory::address_space::validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Paging gaps      : ");
            write_uint64(kernel::memory::address_space::paging_policy_gap_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Runtime policy   : ");
            drivers::vga::write_line(kernel::memory::address_space::runtime_paging_policy_validation_self_test() ? "ok" : "failed");

            for (size_t index = 0; index < kernel::memory::address_space::region_count(); ++index)
            {
                const auto* region = kernel::memory::address_space::region_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(region != nullptr && region->name != nullptr ? region->name : "unnamed");
                drivers::vga::write(" type=");
                drivers::vga::write(region != nullptr ? kernel::memory::address_space::region_type_name(region->type) : "unknown");
                drivers::vga::write(" flags=");
                const uint32_t flags = region != nullptr ? region->flags : 0;
                drivers::vga::write((flags & kernel::memory::paging::PageFlagRead) != 0 ? "r" : "-");
                drivers::vga::write((flags & kernel::memory::paging::PageFlagWrite) != 0 ? "w" : "-");
                drivers::vga::write((flags & kernel::memory::paging::PageFlagUser) != 0 ? "u" : "k");
                drivers::vga::write((flags & kernel::memory::paging::PageFlagExecute) != 0 ? "x" : "-");
                drivers::vga::write(" (");
                write_uint64(region != nullptr ? region->size : 0);
                drivers::vga::write_line(" bytes)");
            }
            return;
        }

        if (core::string::compare(command, "modulesinfo") == 0)
        {
            drivers::vga::write("Modules ready: ");
            drivers::vga::write_line(kernel::initrd::modules::is_ready() ? "yes" : "no");
            drivers::vga::write("Validation   : ");
            drivers::vga::write_line(kernel::initrd::modules::validation_passed() ? "ok" : "failed");
            drivers::vga::write("Declared     : ");
            write_uint64(kernel::initrd::modules::declared_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Module count : ");
            write_uint64(kernel::initrd::modules::count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Rejected     : ");
            write_uint64(kernel::initrd::modules::rejected_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Truncated    : ");
            write_uint64(kernel::initrd::modules::truncated_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Total bytes  : ");
            write_uint64(kernel::initrd::modules::total_bytes());
            drivers::vga::put_char('\n');

            for (size_t index = 0; index < kernel::initrd::modules::count(); ++index)
            {
                const auto* module = kernel::initrd::modules::at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(module != nullptr && module->name != nullptr ? module->name : "unnamed");
                drivers::vga::write(" (");
                write_uint64(module != nullptr ? module->size : 0);
                drivers::vga::write(" bytes checksum=");
                write_uint64(module != nullptr ? module->checksum : 0);
                drivers::vga::write(" metadata=");
                drivers::vga::write(module != nullptr && module->metadata_valid ? "ok" : "failed");
                drivers::vga::write(" vfs=/boot/");
                drivers::vga::write_line(module != nullptr && module->name != nullptr ? module->name : "unnamed");
            }
            return;
        }

        if (core::string::compare(command, "vfsinfo") == 0)
        {
            drivers::vga::write("VFS ready: ");
            drivers::vga::write_line(kernel::vfs::is_ready() ? "yes" : "no");
            drivers::vga::write("Root node: ");
            const auto* root = kernel::vfs::root();
            drivers::vga::write_line(root != nullptr ? root->name : "none");
            return;
        }

        if (core::string::compare(command, "ramfsinfo") == 0)
        {
            drivers::vga::write("RAMFS ready: ");
            drivers::vga::write_line(kernel::vfs::ramfs::is_ready() ? "yes" : "no");

            const auto* root = kernel::vfs::ramfs::root();
            drivers::vga::write("RAMFS root: ");
            drivers::vga::write_line(root != nullptr ? root->name : "none");
            drivers::vga::write("Children  : ");
            write_uint64(kernel::vfs::ramfs::child_count(root));
            drivers::vga::put_char('\n');

            for (size_t index = 0; index < kernel::vfs::ramfs::child_count(root); ++index)
            {
                const auto* child = kernel::vfs::ramfs::child_at(root, index);
                drivers::vga::write("  - ");
                drivers::vga::write_line(child != nullptr ? child->name : "invalid");
            }
            return;
        }

        if (core::string::compare(command, "paginginfo") == 0)
        {
            drivers::vga::write("Paging ready: ");
            drivers::vga::write_line(kernel::memory::paging::is_ready() ? "yes" : "no");
            drivers::vga::write("Page dir  : ");
            write_uint64(kernel::memory::paging::page_directory_address());
            drivers::vga::put_char('\n');
            drivers::vga::write("Runtime   : ");
            drivers::vga::write_line(kernel::memory::paging::is_runtime_enabled() ? "enabled" : "prepared");
            drivers::vga::write("Active dir: ");
            write_uint64(kernel::memory::paging::active_page_directory_address());
            drivers::vga::put_char('\n');
            drivers::vga::write("Mapped MiB: ");
            write_uint64(kernel::memory::paging::mapped_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            drivers::vga::write("Mapped pages: ");
            write_uint64(kernel::memory::paging::mapped_pages());
            drivers::vga::put_char('\n');
            drivers::vga::write("Identity MiB: ");
            write_uint64(kernel::memory::paging::bootstrap_identity_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            drivers::vga::write("Flags      : ");
            const uint32_t flags = kernel::memory::paging::bootstrap_page_flags();
            drivers::vga::write((flags & kernel::memory::paging::PageFlagRead) != 0 ? "r" : "-");
            drivers::vga::write((flags & kernel::memory::paging::PageFlagWrite) != 0 ? "w" : "-");
            drivers::vga::write((flags & kernel::memory::paging::PageFlagUser) != 0 ? "u" : "k");
            drivers::vga::write((flags & kernel::memory::paging::PageFlagExecute) != 0 ? "x" : "-");
            drivers::vga::put_char('\n');
            drivers::vga::write("Self-test  : ");
            drivers::vga::write_line(kernel::memory::paging::validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Policy test: ");
            drivers::vga::write_line(kernel::memory::address_space::runtime_paging_policy_validation_self_test() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "platforminfo") == 0)
        {
            const auto& platform = kernel::platform::requirements::platform();
            drivers::vga::write("Platform : ");
            drivers::vga::write_line(platform.name);
            drivers::vga::write("Machine  : ");
            drivers::vga::write_line(platform.machine_class);
            drivers::vga::write("Boot     : ");
            drivers::vga::write_line(platform.boot_media);
            drivers::vga::write("Console  : ");
            drivers::vga::write_line(platform.console_device);
            drivers::vga::write("Input    : ");
            drivers::vga::write_line(platform.input_device);
            drivers::vga::write("Timer    : ");
            drivers::vga::write_line(platform.timer_device);
            drivers::vga::write("IRQ ctrl : ");
            drivers::vga::write_line(platform.interrupt_controller);
            drivers::vga::write("Storage  : ");
            drivers::vga::write_line(platform.storage_model);
            drivers::vga::write("Drivers  : ");
            drivers::vga::write_line(platform.static_driver_model ? "static" : "dynamic-planned");
            drivers::vga::write("Emulator : ");
            drivers::vga::write_line(platform.emulator_first ? "first" : "optional");
            drivers::vga::write("Self-test: ");
            drivers::vga::write_line(kernel::platform::requirements::platform_validation_self_test() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "pcinfo") == 0)
        {
            drivers::vga::write("PC phases    : ");
            write_uint64(kernel::platform::pc::phase_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Boot critical: ");
            write_uint64(kernel::platform::pc::boot_critical_phase_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Self-test    : ");
            drivers::vga::write_line(kernel::platform::pc::validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Device cover : ");
            write_uint64(kernel::platform::pc::ready_required_device_class_count());
            drivers::vga::write("/");
            write_uint64(kernel::platform::pc::required_device_class_count());
            drivers::vga::put_char('\n');

            for (size_t index = 0; index < kernel::platform::pc::phase_count(); ++index)
            {
                const auto* phase = kernel::platform::pc::phase_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(phase != nullptr && phase->name != nullptr ? phase->name : "invalid");
                drivers::vga::write(" class=");
                drivers::vga::write(phase != nullptr && phase->required_device_class != nullptr ? phase->required_device_class : "unknown");
                drivers::vga::write(" critical=");
                drivers::vga::write(phase != nullptr && phase->boot_critical ? "yes" : "no");
                drivers::vga::put_char('\n');
            }
            drivers::vga::write_line("Required device classes:");
            for (size_t index = 0; index < kernel::platform::pc::required_device_class_count(); ++index)
            {
                const auto* device_class = kernel::platform::pc::required_device_class_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(device_class != nullptr ? kernel::device::class_name(*device_class) : "invalid");
                drivers::vga::write(" ready=");
                drivers::vga::write(device_class != nullptr && kernel::device::has_ready_class(*device_class) ? "yes" : "no");
                drivers::vga::put_char('\n');
            }
            return;
        }

        if (core::string::compare(command, "archinfo") == 0)
        {
            const auto& arch_info = arch::info();
            drivers::vga::write("Arch name : ");
            drivers::vga::write_line(arch_info.name);
            drivers::vga::write("CPU family: ");
            drivers::vga::write_line(arch_info.cpu_family);
            drivers::vga::write("Pointer   : ");
            write_uint64(arch_info.pointer_bits);
            drivers::vga::write_line(" bits");
            drivers::vga::write("Page size : ");
            write_uint64(arch_info.page_size);
            drivers::vga::put_char('\n');
            drivers::vga::write("Protected : ");
            drivers::vga::write_line(arch_info.protected_mode ? "yes" : "no");
            drivers::vga::write("Paging    : ");
            drivers::vga::write_line(arch_info.paging_supported ? "yes" : "no");
            drivers::vga::write("NX        : ");
            drivers::vga::write_line(arch_info.nx_supported ? "yes" : "no");
            drivers::vga::write("Endian    : ");
            drivers::vga::write_line(arch_info.little_endian ? "little" : "big/unknown");
            drivers::vga::write("Self-test : ");
            drivers::vga::write_line(arch::validation_self_test() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "frameinfo") == 0)
        {
            drivers::vga::write("Frames total: ");
            write_uint64(kernel::memory::frames::total_frames());
            drivers::vga::put_char('\n');
            drivers::vga::write("Frames free : ");
            write_uint64(kernel::memory::frames::free_frames());
            drivers::vga::put_char('\n');
            drivers::vga::write("Frames used : ");
            write_uint64(kernel::memory::frames::reserved_frames());
            drivers::vga::put_char('\n');
            drivers::vga::write("Alloc failures: ");
            write_uint64(kernel::memory::frames::allocation_failure_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Invalid frees : ");
            write_uint64(kernel::memory::frames::invalid_free_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Double frees  : ");
            write_uint64(kernel::memory::frames::double_free_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Accounting    : ");
            drivers::vga::write_line(kernel::memory::frames::accounting_valid() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "heapinfo") == 0)
        {
            drivers::vga::write("Heap total: ");
            write_uint64(kernel::memory::heap::total_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("Heap free : ");
            write_uint64(kernel::memory::heap::free_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("Heap used : ");
            write_uint64(kernel::memory::heap::used_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("Heap blocks: ");
            write_uint64(kernel::memory::heap::block_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Alloc calls: ");
            write_uint64(kernel::memory::heap::allocation_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Free calls : ");
            write_uint64(kernel::memory::heap::free_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Invalid frees: ");
            write_uint64(kernel::memory::heap::invalid_free_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Double frees : ");
            write_uint64(kernel::memory::heap::double_free_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Corrupt blocks: ");
            write_uint64(kernel::memory::heap::corrupt_block_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("State      : ");
            drivers::vga::write_line(kernel::memory::heap::state_valid() ? "ok" : "failed");
            return;
        }

        if (core::string::compare(command, "heaptest") == 0)
        {
            void* first = kernel::memory::heap::allocate(64);
            void* second = kernel::memory::heap::allocate(256);
            char text[16];
            core::memory::string_copy_safe(text, sizeof(text), "heap-ok");
            TINYOS_WARN_ON(core::string::compare(text, "heap-ok") != 0, "Safe string copy validation failed.");

            if (first == nullptr || second == nullptr)
            {
                drivers::vga::write_line("Heap self-test failed.");
                return;
            }

            kernel::memory::heap::free(second);
            kernel::memory::heap::free(first);
            drivers::vga::write_line(kernel::memory::heap::state_valid() ? "Heap self-test passed." : "Heap self-test failed: invalid state.");
            return;
        }

        if (core::string::compare(command, "irqinfo") == 0)
        {
            drivers::vga::write("PIC initialized       : ");
            drivers::vga::write_line(drivers::pic::is_initialized() ? "yes" : "no");
            drivers::vga::write("Hardware IRQs enabled: ");
            drivers::vga::write_line(kernel::interrupts::hardware_irq_enabled() ? "yes" : "no");
            drivers::vga::write("IRQ0 masked          : ");
            drivers::vga::write_line(drivers::pic::is_masked(0) ? "yes" : "no");
            drivers::vga::write("IRQ1 masked          : ");
            drivers::vga::write_line(drivers::pic::is_masked(1) ? "yes" : "no");
            drivers::vga::write("Total IRQs          : ");
            write_uint64(kernel::interrupts::total_irq_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Unexpected IRQs     : ");
            write_uint64(kernel::interrupts::unexpected_irq_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Last IRQ            : ");
            if (kernel::interrupts::has_seen_irq())
            {
                write_uint64(kernel::interrupts::last_irq());
                drivers::vga::put_char('\n');
            }
            else
            {
                drivers::vga::write_line("none");
            }

            for (uint8_t irq = 0; irq < kernel::interrupts::IrqLineCount; ++irq)
            {
                drivers::vga::write("  IRQ");
                write_uint64(irq);
                drivers::vga::write(": ");
                write_uint64(kernel::interrupts::irq_count(irq));
                drivers::vga::put_char('\n');
            }
            return;
        }

        if (core::string::compare(command, "inputinfo") == 0)
        {
            drivers::vga::write("Pending input events: ");
            write_uint64(drivers::input::pending_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Input queue capacity: ");
            write_uint64(drivers::input::queue_capacity());
            drivers::vga::put_char('\n');
            drivers::vga::write("Dropped input events: ");
            write_uint64(drivers::input::dropped_event_count());
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "keyboardinfo") == 0)
        {
            drivers::vga::write("Interrupt input: ");
            drivers::vga::write_line(drivers::keyboard::interrupt_input_enabled() ? "enabled" : "disabled");
            drivers::vga::write("IRQ1 masked    : ");
            drivers::vga::write_line(drivers::pic::is_masked(1) ? "yes" : "no");
            drivers::vga::write("Buffered chars : ");
            write_uint64(drivers::keyboard::buffered_character_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("IRQ scancodes  : ");
            write_uint64(drivers::keyboard::irq_scancode_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Polled scancodes: ");
            write_uint64(drivers::keyboard::polled_scancode_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Ignored scancodes: ");
            write_uint64(drivers::keyboard::ignored_scancode_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Dropped chars  : ");
            write_uint64(drivers::keyboard::dropped_character_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Last scancode  : ");
            if (drivers::keyboard::has_seen_scancode())
            {
                write_uint64(drivers::keyboard::last_scancode());
                drivers::vga::put_char('\n');
            }
            else
            {
                drivers::vga::write_line("none");
            }
            return;
        }

        if (core::string::compare(command, "inputpeek") == 0)
        {
            drivers::input::Event event{};
            if (!drivers::input::poll_event(event))
            {
                drivers::vga::write_line("No pending input events.");
                return;
            }

            drivers::vga::write("Input event: ");
            if (event.type == drivers::input::EventType::Key)
            {
                drivers::vga::write("key '");
                drivers::vga::put_char(event.character);
                drivers::vga::write_line("'");
            }
            else
            {
                drivers::vga::write_line("unknown");
            }
            return;
        }

        if (core::string::compare(command, "help") == 0 || core::string::compare(command, "helpui") == 0)
        {
            run_help_ui();
            return;
        }

        if (core::string::starts_with(command, "helpsearch "))
        {
            print_help_search(command + 10);
            return;
        }

        if (core::string::compare(command, "helpsearch") == 0)
        {
            drivers::vga::write_line("Usage: helpsearch <text>");
            return;
        }

        if (core::string::compare(command, "status") == 0)
        {
            print_terminal_status();
            return;
        }

        if (core::string::compare(command, "syscheck") == 0)
        {
            run_system_check();
            return;
        }

        if (core::string::compare(command, "helplist") == 0)
        {
            print_help_list();
            return;
        }

        if (core::string::compare(command, "fileui") == 0)
        {
            run_file_ui();
            return;
        }

        if (core::string::compare(command, "filemgr") == 0)
        {
            run_file_manager();
            return;
        }

        if (core::string::compare(command, "clear") == 0)
        {
            drivers::vga::clear();
            return;
        }

        if (core::string::compare(command, "version") == 0)
        {
            drivers::vga::write(config::Name);
            drivers::vga::write(" version ");
            drivers::vga::write(config::Version);
            drivers::vga::write(" (");
            drivers::vga::write(config::Architecture);
            drivers::vga::write_line(")");
            return;
        }

        if (core::string::compare(command, "echo") == 0)
        {
            drivers::vga::write_line("");
            return;
        }

        if (core::string::starts_with(command, "echo "))
        {
            drivers::vga::write_line(core::string::skip_spaces(command + 4));
            return;
        }

        if (core::string::compare(command, "reboot") == 0)
        {
            drivers::vga::write_line("Rebooting...");
            arch::reboot();
        }

        if (core::string::compare(command, "meminfo") == 0)
        {
            drivers::vga::write("Regions: ");
            write_uint64(kernel::memory::map::region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Total MiB: ");
            write_uint64(kernel::memory::map::total_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            drivers::vga::write("Usable MiB: ");
            write_uint64(kernel::memory::map::usable_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "uptime") == 0)
        {
            drivers::vga::write("Ticks: ");
            write_uint64(drivers::pit::ticks());
            drivers::vga::write(" @ ");
            write_uint64(drivers::pit::frequency());
            drivers::vga::write_line(" Hz");
            return;
        }

        if (core::string::compare(command, "timerinfo") == 0)
        {
            drivers::vga::write("PIT configured: ");
            drivers::vga::write_line(drivers::pit::is_configured() ? "yes" : "no");
            drivers::vga::write("Frequency     : ");
            write_uint64(drivers::pit::frequency());
            drivers::vga::write_line(" Hz");
            drivers::vga::write("Ticks         : ");
            write_uint64(drivers::pit::ticks());
            drivers::vga::put_char('\n');
            drivers::vga::write("IRQ0 count    : ");
            write_uint64(kernel::interrupts::irq_count(0));
            drivers::vga::put_char('\n');
            drivers::vga::write("IRQ0 masked   : ");
            drivers::vga::write_line(drivers::pic::is_masked(0) ? "yes" : "no");
            drivers::vga::write("IRQ enabled   : ");
            drivers::vga::write_line(kernel::interrupts::hardware_irq_enabled() ? "yes" : "no");
            drivers::vga::write("Scheduler ticks: ");
            write_uint64(kernel::sched::tick_count());
            drivers::vga::put_char('\n');
            return;
        }

        if (core::string::compare(command, "yield") == 0)
        {
            kernel::sched::yield();
            drivers::vga::write_line("Yield recorded.");
            return;
        }

        if (core::string::compare(command, "sleeptest") == 0)
        {
            drivers::vga::write_line("Sleeping for 10 PIT ticks...");
            kernel::sched::sleep_ticks(10);
            drivers::vga::write_line("Sleep complete.");
            return;
        }

        if (core::string::compare(command, "int3") == 0)
        {
            asm volatile ("int3");
            return;
        }

        if (core::string::compare(command, "panic") == 0)
        {
            kernel::panic("Manual panic requested from shell.");
        }

        drivers::vga::write("Unknown command: ");
        drivers::vga::write_line(command);
        drivers::vga::write_line("Type 'help' for a list of commands.");
    }
}
