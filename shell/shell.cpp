#include <stddef.h>

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
#include <tinyos/kernel/memory/address_space.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/heap.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/memory/paging.hpp>
#include <tinyos/kernel/panic.hpp>
#include <tinyos/kernel/platform/requirements.hpp>
#include <tinyos/kernel/provision/image.hpp>
#include <tinyos/kernel/sched/scheduler.hpp>
#include <tinyos/kernel/security/integrity.hpp>
#include <tinyos/kernel/security/trust.hpp>
#include <tinyos/kernel/syscall/syscall.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/kernel/user/transition.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/shell/shell.hpp>
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>
#include <tinyos/ui/widgets.hpp>

namespace
{
    constexpr size_t MaxInputLength = 128;
    constexpr size_t MaxPathLength = 96;

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
        if (node->directory)
        {
            tinyos::drivers::vga::write("Children : ");
            write_uint64(tinyos::kernel::vfs::child_count(node));
            tinyos::drivers::vga::put_char('\n');
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
        tinyos::drivers::vga::write("Name        : ");
        tinyos::drivers::vga::write_line(device != nullptr && device->name != nullptr ? device->name : "none");
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
        tinyos::drivers::vga::write("Read-only path   : /volumes/ram-block0/volume.txt\n");
        tinyos::drivers::vga::write("Self-test        : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::vfs::blockfs::validation_self_test() ? "ok" : "failed");
    }

    void print_framebuffer_info()
    {
        const auto* surface = tinyos::kernel::device::framebuffer::active_surface();
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
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::has_linear_framebuffer() ? "yes" : "not yet");
        tinyos::drivers::vga::write("Self-test     : ");
        tinyos::drivers::vga::write_line(tinyos::kernel::device::framebuffer::validation_self_test() ? "ok" : "failed");
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
        tinyos::drivers::vga::write("Draw calls     : ");
        write_uint64(tinyos::ui::renderer::draw_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Primitive calls: ");
        write_uint64(tinyos::ui::renderer::primitive_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Rejected draws : ");
        write_uint64(tinyos::ui::renderer::rejected_draw_call_count());
        tinyos::drivers::vga::put_char('\n');
        tinyos::drivers::vga::write("Self-test      : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::validation_self_test() ? "ok" : "failed");
        tinyos::drivers::vga::write("Primitives     : ");
        tinyos::drivers::vga::write_line(tinyos::ui::renderer::primitive_validation_self_test() ? "ok" : "failed");
    }

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
        tinyos::drivers::vga::write_line("  app bundle -> system profile -> image manifest -> build -> sign -> encrypt -> deploy -> verify -> rollback");
        tinyos::drivers::vga::write_line("Host entry point:");
        tinyos::drivers::vga::write_line("  scripts/tinyos-image.sh plan|check-profile|build|manifest|keygen|sign|encrypt|deploy");
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

    void print_help()
    {
        tinyos::drivers::vga::write_line("Available commands:");
        tinyos::drivers::vga::write_line("  help     - show this help");
        tinyos::drivers::vga::write_line("  clear    - clear the screen");
        tinyos::drivers::vga::write_line("  files    - list TinyOS RAMFS path");
        tinyos::drivers::vga::write_line("  fsmap    - show TinyOS RAMFS tree");
        tinyos::drivers::vga::write_line("  show     - print TinyOS RAMFS file");
        tinyos::drivers::vga::write_line("  describe - show TinyOS RAMFS node details");
        tinyos::drivers::vga::write_line("  write    - overwrite writable TinyOS RAMFS file");
        tinyos::drivers::vga::write_line("  aliases  - show compatibility command aliases");
        tinyos::drivers::vga::write_line("  devices  - show TinyOS device registry");
        tinyos::drivers::vga::write_line("  device   - show one registered device");
        tinyos::drivers::vga::write_line("  blockinfo - show RAM block device scaffold");
        tinyos::drivers::vga::write_line("  storageinfo - show block VFS mount scaffold");
        tinyos::drivers::vga::write_line("  fbinfo   - show framebuffer surface scaffold");
        tinyos::drivers::vga::write_line("  renderinfo - show renderer scaffold state");
        tinyos::drivers::vga::write_line("  rendertest - draw a renderer test label");
        tinyos::drivers::vga::write_line("  renderfilltest - draw a renderer filled strip");
        tinyos::drivers::vga::write_line("  terminalinfo - show terminal UI scaffold state");
        tinyos::drivers::vga::write_line("  terminaltest - draw terminal UI test labels");
        tinyos::drivers::vga::write_line("  terminalclear - clear terminal UI regions");
        tinyos::drivers::vga::write_line("  terminalpaneltest - draw terminal UI panel");
        tinyos::drivers::vga::write_line("  widgetinfo - show TUI widget scaffold state");
        tinyos::drivers::vga::write_line("  widgettest - draw TUI widget demo");
        tinyos::drivers::vga::write_line("  widgetdispatch - dispatch queued UI events to widgets");
        tinyos::drivers::vga::write_line("  widgetactiontest - inject and dispatch widget action");
        tinyos::drivers::vga::write_line("  uieventinfo - show UI event queue state");
        tinyos::drivers::vga::write_line("  uieventpump - move input events into UI queue");
        tinyos::drivers::vga::write_line("  uieventpeek - read one UI event");
        tinyos::drivers::vga::write_line("  uieventtest - inject a UI test key event");
        tinyos::drivers::vga::write_line("  tools    - list system management tools");
        tinyos::drivers::vga::write_line("  toolinfo - show management tool summary");
        tinyos::drivers::vga::write_line("  tool     - show one management tool");
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
        tinyos::drivers::vga::write_line("  requirements - show minimum system requirements");
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
        tinyos::drivers::vga::write_line("  sysinfo  - show syscall ABI scaffold status");
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
            list_path("/");
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

            list_path(path);
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

            print_tree(kernel::vfs::find(path), 0);
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

            show_file(path);
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

            show_file_info(path);
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

            edit_file(path, text);
            return;
        }

        if (core::string::compare(command, "aliases") == 0)
        {
            drivers::vga::write_line("Compatibility aliases:");
            drivers::vga::write_line("  ls       -> files");
            drivers::vga::write_line("  tree     -> fsmap");
            drivers::vga::write_line("  cat/view -> show");
            drivers::vga::write_line("  fileinfo -> describe");
            drivers::vga::write_line("  edit     -> write");
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
            drivers::vga::write("Syscall ready: ");
            drivers::vga::write_line(kernel::syscall::is_ready() ? "yes" : "no");
            drivers::vga::write("Syscall count: ");
            write_uint64(kernel::syscall::count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Max buffer   : ");
            write_uint64(kernel::syscall::max_user_buffer_bytes());
            drivers::vga::put_char('\n');
            drivers::vga::write("Validation   : ");
            drivers::vga::write_line(kernel::syscall::validation_self_test() ? "ok" : "failed");
            drivers::vga::write("Bad buffers  : ");
            write_uint64(kernel::syscall::validation_failure_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Rejected calls: ");
            write_uint64(kernel::syscall::rejected_call_count());
            drivers::vga::put_char('\n');
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
            drivers::vga::write("Context switches: ");
            write_uint64(kernel::sched::context_switch_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Contexts ready  : ");
            write_uint64(kernel::task::prepared_context_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Preemption      : ");
            drivers::vga::write_line(kernel::sched::preemption_enabled() ? "enabled" : "not yet");
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
            return;
        }

        if (core::string::compare(command, "addrspaceinfo") == 0)
        {
            drivers::vga::write("Address space ready: ");
            drivers::vga::write_line(kernel::memory::address_space::is_ready() ? "yes" : "no");
            drivers::vga::write("Region count      : ");
            write_uint64(kernel::memory::address_space::region_count());
            drivers::vga::put_char('\n');
            drivers::vga::write("Mapped total MiB : ");
            write_uint64(kernel::memory::address_space::total_mapped_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');

            for (size_t index = 0; index < kernel::memory::address_space::region_count(); ++index)
            {
                const auto* region = kernel::memory::address_space::region_at(index);
                drivers::vga::write("  - ");
                drivers::vga::write(region != nullptr && region->name != nullptr ? region->name : "unnamed");
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
                drivers::vga::write_line(module != nullptr && module->metadata_valid ? "ok" : "failed");
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
            drivers::vga::write("Mapped MiB: ");
            write_uint64(kernel::memory::paging::mapped_bytes() / (1024 * 1024));
            drivers::vga::put_char('\n');
            drivers::vga::write("Mapped pages: ");
            write_uint64(kernel::memory::paging::mapped_pages());
            drivers::vga::put_char('\n');
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

        if (core::string::compare(command, "help") == 0)
        {
            print_help();
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
