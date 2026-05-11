#include <tinyos/api/system_api.hpp>
#include <tinyos/boot/multiboot.hpp>
#include <tinyos/arch/interrupts.hpp>
#include <tinyos/arch/hal.hpp>
#include <tinyos/config.hpp>
#include <tinyos/drivers/input.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/drivers/pic.hpp>
#include <tinyos/drivers/pit.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/admin/tools.hpp>
#include <tinyos/kernel/device/block.hpp>
#include <tinyos/kernel/device/framebuffer.hpp>
#include <tinyos/kernel/device/registry.hpp>
#include <tinyos/kernel/app/launcher.hpp>
#include <tinyos/kernel/app/manifest.hpp>
#include <tinyos/kernel/app/package.hpp>
#include <tinyos/kernel/app/package_verifier.hpp>
#include <tinyos/kernel/app/runtime.hpp>
#include <tinyos/kernel/elf/loader.hpp>
#include <tinyos/kernel/security/integrity.hpp>
#include <tinyos/kernel/security/trust.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/interrupts.hpp>
#include <tinyos/kernel/kernel.hpp>
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
#include <tinyos/kernel/syscall/syscall.hpp>
#include <tinyos/kernel/task/task.hpp>
#include <tinyos/kernel/user/transition.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/shell/shell.hpp>
#include <tinyos/ui/events.hpp>
#include <tinyos/ui/desktop.hpp>
#include <tinyos/ui/renderer.hpp>
#include <tinyos/ui/terminal.hpp>
#include <tinyos/ui/window_manager.hpp>
#include <tinyos/ui/widgets.hpp>

namespace
{
    constexpr uint64_t TimerStabilityTicks = 5;
    constexpr uint32_t TimerStabilitySpinLimit = 20000000;

    void debug_boot_checkpoint(const char* stage)
    {
#if defined(TINYOS_DEBUG_BOOT)
        tinyos::drivers::serial::write("[debug-boot] ");
        tinyos::drivers::serial::write_line(stage);
#else
        (void)stage;
#endif
    }

    bool wait_for_timer_stability()
    {
        const uint64_t start_ticks = tinyos::drivers::pit::ticks();

        for (uint32_t spin = 0; spin < TimerStabilitySpinLimit; ++spin)
        {
            if ((tinyos::drivers::pit::ticks() - start_ticks) >= TimerStabilityTicks)
            {
                return true;
            }

            asm volatile ("pause");
        }

        return false;
    }

    void register_device_or_panic(const char* name, tinyos::kernel::device::Class device_class, tinyos::kernel::device::State state, uint32_t unit, uint32_t flags)
    {
        TINYOS_ASSERT(tinyos::kernel::device::register_device(name, device_class, state, unit, flags), "Device registry rejected a core device.");
    }
}

extern "C" void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    (void)multiboot_info_addr;

    tinyos::arch::initialize();
    TINYOS_ASSERT(tinyos::arch::validation_self_test(), "Architecture capability manifest validation failed.");
    tinyos::drivers::vga::initialize();
    tinyos::drivers::serial::initialize();
    debug_boot_checkpoint("serial ready");
    tinyos::kernel::klog::initialize();
    debug_boot_checkpoint("kernel logger ready");
    TINYOS_ASSERT(tinyos::kernel::platform::requirements::validation_self_test(), "System requirements manifest validation failed.");
    TINYOS_ASSERT(tinyos::kernel::platform::pc::validation_self_test(), "PC platform initialization contract validation failed.");
    tinyos::kernel::device::initialize();
    register_device_or_panic("vga-text", tinyos::kernel::device::Class::Console, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagBootCritical | tinyos::kernel::device::FlagHardware);
    tinyos::kernel::device::framebuffer::initialize_text_grid("vga-text-grid", 80, 25, 0xB8000, 2);
    TINYOS_ASSERT(tinyos::kernel::device::framebuffer::validation_self_test(), "Framebuffer surface scaffold validation failed.");
    register_device_or_panic("vga-text-grid", tinyos::kernel::device::Class::Framebuffer, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagBootCritical | tinyos::kernel::device::FlagHardware | tinyos::kernel::device::FlagWritable);
    tinyos::ui::renderer::initialize();
    TINYOS_ASSERT(tinyos::ui::renderer::validation_self_test(), "Renderer scaffold validation failed.");
    TINYOS_ASSERT(tinyos::ui::renderer::primitive_validation_self_test(), "Renderer primitive scaffold validation failed.");
    tinyos::ui::terminal::initialize();
    TINYOS_ASSERT(tinyos::ui::terminal::validation_self_test(), "Terminal UI scaffold validation failed.");
    TINYOS_ASSERT(tinyos::ui::terminal::panel_validation_self_test(), "Terminal panel scaffold validation failed.");
    tinyos::ui::widgets::initialize();
    TINYOS_ASSERT(tinyos::ui::widgets::validation_self_test(), "TUI widget scaffold validation failed.");
    tinyos::ui::window_manager::initialize();
    TINYOS_ASSERT(tinyos::ui::window_manager::validation_self_test(), "Window manager scaffold validation failed.");
    TINYOS_ASSERT(tinyos::ui::window_manager::composition_validation_self_test(), "Window manager composition validation failed.");
    tinyos::ui::desktop::initialize();
    TINYOS_ASSERT(tinyos::ui::desktop::validation_self_test(), "Desktop shell prototype validation failed.");
    TINYOS_ASSERT(tinyos::ui::desktop::launcher_validation_self_test(), "Desktop launcher validation failed.");
    TINYOS_ASSERT(tinyos::ui::desktop::interaction_validation_self_test(), "Desktop launcher interaction validation failed.");
    TINYOS_ASSERT(tinyos::ui::desktop::fullscreen_validation_self_test(), "Fullscreen desktop validation failed.");
    register_device_or_panic("serial-com1", tinyos::kernel::device::Class::Diagnostics, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagBootCritical | tinyos::kernel::device::FlagHardware | tinyos::kernel::device::FlagDiagnostics);
    debug_boot_checkpoint("device registry ready");

    TINYOS_ASSERT(multiboot_magic == tinyos::boot::multiboot::BootloaderMagic, "Invalid Multiboot magic.");
    debug_boot_checkpoint("multiboot header validated");

    tinyos::arch::interrupts::initialize();
    debug_boot_checkpoint("interrupt descriptor table ready");
    tinyos::kernel::interrupts::initialize_diagnostics();
    debug_boot_checkpoint("irq diagnostics ready");
    tinyos::drivers::pic::initialize();
    register_device_or_panic("pic-8259", tinyos::kernel::device::Class::InterruptController, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagBootCritical | tinyos::kernel::device::FlagHardware);
    debug_boot_checkpoint("pic initialized with all irq lines masked");
    tinyos::kernel::memory::map::initialize(multiboot_info_addr);
    debug_boot_checkpoint("memory map parsed");
    tinyos::kernel::initrd::modules::initialize(multiboot_info_addr);
    debug_boot_checkpoint("boot modules parsed");
    TINYOS_ASSERT(tinyos::kernel::initrd::modules::validation_passed(), "Boot module metadata validation failed.");
    tinyos::kernel::elf::loader::initialize();
    debug_boot_checkpoint("elf loader scaffold ready");
    TINYOS_ASSERT(tinyos::kernel::elf::loader::validation_passed(), "ELF loader validation failed.");
    TINYOS_ASSERT(tinyos::kernel::elf::loader::validation_self_test(), "ELF loader validation self-test failed.");
    tinyos::kernel::memory::frames::initialize(multiboot_info_addr);
    debug_boot_checkpoint("frame allocator ready");
    tinyos::kernel::memory::heap::initialize();
    debug_boot_checkpoint("kernel heap ready");

    void* heap_test_block = tinyos::kernel::memory::heap::allocate(64);
    TINYOS_ASSERT(heap_test_block != nullptr, "Kernel heap self-test allocation failed.");
    tinyos::kernel::memory::heap::free(heap_test_block);
    TINYOS_ASSERT(tinyos::kernel::memory::heap::state_valid(), "Kernel heap state validation failed.");
    debug_boot_checkpoint("kernel heap self-test complete");

    tinyos::kernel::memory::address_space::initialize(multiboot_info_addr);
    debug_boot_checkpoint("address space scaffold ready");
    tinyos::kernel::memory::paging::initialize();
    debug_boot_checkpoint("paging structures prepared");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::is_ready(), "Address space scaffold did not initialize.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::validation_self_test(), "Address space protection flag self-test failed.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::kernel_section_region_count() >= 4, "Kernel section address-space contract incomplete.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::boot_module_region_count() == tinyos::kernel::initrd::modules::count(), "Boot module address-space tracking mismatch.");
    TINYOS_ASSERT(tinyos::kernel::memory::paging::is_ready(), "Paging structures did not initialize.");
    TINYOS_ASSERT(tinyos::kernel::memory::paging::page_directory_address() != 0, "Paging directory address missing.");
    TINYOS_ASSERT(tinyos::kernel::memory::paging::validation_self_test(), "Paging protection flag self-test failed.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::paging_policy_gap_count() > 0, "Paging policy gap diagnostics did not detect bootstrap broad mapping.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::apply_paging_policy() > 0, "Address-space paging policy did not update bootstrap tables.");
    TINYOS_ASSERT(tinyos::kernel::memory::address_space::paging_policy_gap_count() == 0, "Address-space paging policy still has enforceable gaps.");
    tinyos::kernel::memory::paging::enable_runtime();
    TINYOS_ASSERT(tinyos::kernel::memory::paging::is_runtime_enabled(), "Runtime paging did not enable.");
    TINYOS_ASSERT(tinyos::kernel::memory::paging::active_page_directory_address() == tinyos::kernel::memory::paging::page_directory_address(), "Runtime paging CR3 mismatch.");

    tinyos::kernel::device::block::initialize();
    TINYOS_ASSERT(tinyos::kernel::device::block::validation_self_test(), "Block device scaffold validation failed.");
    register_device_or_panic("ram-block0", tinyos::kernel::device::Class::Block, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagVirtual | tinyos::kernel::device::FlagReadable | tinyos::kernel::device::FlagWritable);
    debug_boot_checkpoint("block device scaffold ready");
    tinyos::kernel::vfs::initialize();
    register_device_or_panic("ramfs", tinyos::kernel::device::Class::Filesystem, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagVirtual | tinyos::kernel::device::FlagWritable);
    TINYOS_ASSERT(tinyos::kernel::vfs::validation_self_test(), "VFS path validation failed.");
    TINYOS_ASSERT(tinyos::kernel::vfs::block_mount_ready(), "Block VFS mount scaffold did not initialize.");
    debug_boot_checkpoint("vfs ready");
    const auto* ramfs_notes = tinyos::kernel::vfs::find("/users/notes.txt");
    const auto* ramfs_block = tinyos::kernel::vfs::find("/devices/ram-block0");
    const auto* ramfs_framebuffer = tinyos::kernel::vfs::find("/devices/vga-text-grid");
    const auto* ramfs_tapp_info = tinyos::kernel::vfs::find("/system/tapp.txt");
    const auto* ramfs_trust_info = tinyos::kernel::vfs::find("/system/trust.txt");
    const auto* ramfs_example_tapp = tinyos::kernel::vfs::find("/apps/example-system-tool.tapp");
    const auto* block_volume_info = tinyos::kernel::vfs::find("/volumes/ram-block0/volume.txt");
    const char* block_volume_text = nullptr;
    size_t block_volume_size = 0;
    TINYOS_ASSERT(ramfs_notes != nullptr && !ramfs_notes->directory && ramfs_notes->writable, "RAMFS user file scaffold missing.");
    TINYOS_ASSERT(ramfs_block != nullptr && !ramfs_block->directory, "RAMFS block device metadata missing.");
    TINYOS_ASSERT(ramfs_framebuffer != nullptr && !ramfs_framebuffer->directory, "RAMFS framebuffer metadata missing.");
    TINYOS_ASSERT(ramfs_tapp_info != nullptr && !ramfs_tapp_info->directory, "RAMFS TAPP metadata missing.");
    TINYOS_ASSERT(ramfs_trust_info != nullptr && !ramfs_trust_info->directory, "RAMFS trust metadata missing.");
    TINYOS_ASSERT(ramfs_example_tapp != nullptr && !ramfs_example_tapp->directory, "RAMFS example TAPP missing.");
    TINYOS_ASSERT(block_volume_info != nullptr && !block_volume_info->directory, "Block VFS volume metadata missing.");
    TINYOS_ASSERT(tinyos::kernel::vfs::read_file(block_volume_info, block_volume_text, block_volume_size) && block_volume_text != nullptr && block_volume_size != 0, "Block VFS volume metadata unreadable.");
    tinyos::kernel::syscall::initialize();
    debug_boot_checkpoint("syscall abi ready");
    TINYOS_ASSERT(tinyos::kernel::syscall::validation_self_test(), "Syscall validation self-test failed.");
    TINYOS_ASSERT(tinyos::kernel::syscall::boundary_policy_validation_self_test(), "Syscall boundary policy validation failed.");
    TINYOS_ASSERT(tinyos::kernel::syscall::definition_validation_self_test(), "Syscall definition table validation failed.");
    TINYOS_ASSERT(tinyos::kernel::syscall::filter_policy_validation_self_test(), "Syscall filter policy validation failed.");
    TINYOS_ASSERT(tinyos::kernel::syscall::resource_policy_validation_self_test(), "Syscall resource policy validation failed.");
    tinyos::kernel::app::runtime::initialize();
    debug_boot_checkpoint("language runtime manifest ready");
    TINYOS_ASSERT(tinyos::kernel::app::runtime::validation_self_test(), "Language runtime manifest validation failed.");
    tinyos::kernel::app::manifest::initialize();
    debug_boot_checkpoint("application capability profile manifest ready");
    TINYOS_ASSERT(tinyos::kernel::app::manifest::validation_self_test(), "Application capability profile validation failed.");
    tinyos::kernel::app::package::initialize();
    debug_boot_checkpoint("tapp package registry ready");
    TINYOS_ASSERT(tinyos::kernel::app::package::validation_self_test(), "TAPP package registry validation failed.");
    tinyos::kernel::security::trust::initialize();
    debug_boot_checkpoint("tapp trust store ready");
    TINYOS_ASSERT(tinyos::kernel::security::trust::validation_self_test(), "TAPP trust store validation failed.");
    tinyos::kernel::app::package_verifier::initialize();
    debug_boot_checkpoint("tapp package verifier ready");
    TINYOS_ASSERT(tinyos::kernel::app::package_verifier::validation_self_test(), "TAPP package verifier validation failed.");
    tinyos::kernel::app::launcher::initialize();
    debug_boot_checkpoint("application launch policy ready");
    TINYOS_ASSERT(tinyos::kernel::app::launcher::validation_self_test(), "Application launch policy validation failed.");
    tinyos::kernel::admin::tools::initialize();
    debug_boot_checkpoint("system management tools manifest ready");
    TINYOS_ASSERT(tinyos::kernel::admin::tools::validation_self_test(), "System management tools manifest validation failed.");
    tinyos::kernel::provision::image::initialize();
    debug_boot_checkpoint("secure image provisioning manifest ready");
    TINYOS_ASSERT(tinyos::kernel::provision::image::validation_self_test(), "Secure image provisioning manifest validation failed.");
    tinyos::kernel::user::transition::initialize();
    debug_boot_checkpoint("user transition scaffold ready");
    tinyos::kernel::security::integrity::initialize();
    debug_boot_checkpoint("security integrity scaffold ready");
    TINYOS_ASSERT(tinyos::kernel::security::integrity::boot_modules_valid(), "Boot module integrity self-test failed.");
    tinyos::kernel::task::initialize();
    debug_boot_checkpoint("task scaffold ready");
    TINYOS_ASSERT(tinyos::kernel::task::owned_kernel_stack_count() == tinyos::kernel::task::task_count(), "Kernel task stack ownership check failed.");
    TINYOS_ASSERT(tinyos::kernel::task::contexts_ready(), "Kernel task context preparation failed.");
    tinyos::kernel::sched::initialize();
    debug_boot_checkpoint("scheduler scaffold ready");
    tinyos::drivers::pit::initialize(100);
    register_device_or_panic("pit", tinyos::kernel::device::Class::Timer, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagBootCritical | tinyos::kernel::device::FlagHardware | tinyos::kernel::device::FlagInterruptDriven);
    debug_boot_checkpoint("pit configured");
    tinyos::drivers::input::initialize();
    register_device_or_panic("input-queue", tinyos::kernel::device::Class::Input, tinyos::kernel::device::State::Ready, 0, tinyos::kernel::device::FlagVirtual);
    debug_boot_checkpoint("generic input queue ready");
    tinyos::ui::events::initialize();
    TINYOS_ASSERT(tinyos::ui::events::validation_self_test(), "UI event queue scaffold validation failed.");
    TINYOS_ASSERT(tinyos::ui::widgets::event_bridge_validation_self_test(), "TUI widget event bridge validation failed.");
    TINYOS_ASSERT(tinyos::ui::desktop::input_validation_self_test(), "Desktop input interaction validation failed.");
    register_device_or_panic("ui-event-queue", tinyos::kernel::device::Class::Input, tinyos::kernel::device::State::Ready, 2, tinyos::kernel::device::FlagVirtual);
    tinyos::drivers::keyboard::initialize();
    register_device_or_panic("keyboard-ps2", tinyos::kernel::device::Class::Input, tinyos::kernel::device::State::Ready, 1, tinyos::kernel::device::FlagHardware | tinyos::kernel::device::FlagInterruptDriven);
    debug_boot_checkpoint("keyboard driver ready");

    TINYOS_ASSERT(tinyos::kernel::device::count() >= 9, "Device registry core devices missing.");
    TINYOS_ASSERT(tinyos::kernel::device::ready_count() == tinyos::kernel::device::count(), "Device registry contains non-ready core devices.");
    TINYOS_ASSERT(tinyos::kernel::device::has_ready_class(tinyos::kernel::device::Class::Block), "Block device class missing from registry.");
    TINYOS_ASSERT(tinyos::kernel::device::has_ready_class(tinyos::kernel::device::Class::Framebuffer), "Framebuffer device class missing from registry.");
    TINYOS_ASSERT(tinyos::kernel::platform::pc::device_contract_satisfied(), "PC required device class contract missing ready devices.");

    TINYOS_ASSERT(tinyos::kernel::security::integrity::allocator_state_valid(), "Allocator integrity self-test failed.");
    debug_boot_checkpoint("allocator integrity self-test complete");

    tinyos::drivers::pic::clear_mask(0);
    tinyos::drivers::keyboard::enable_interrupt_input();
    tinyos::drivers::pic::clear_mask(1);
    tinyos::arch::interrupts::enable();
    tinyos::kernel::interrupts::set_hardware_irq_enabled(true);
    TINYOS_ASSERT(!tinyos::drivers::pic::is_masked(0), "PIT IRQ0 is still masked after rollout.");
    TINYOS_ASSERT(!tinyos::drivers::pic::is_masked(1), "Keyboard IRQ1 is still masked after rollout.");
    TINYOS_ASSERT(wait_for_timer_stability(), "PIT IRQ0 stability check failed.");
    debug_boot_checkpoint("pit irq0 stability check complete");

    tinyos::api::clear_screen();
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TinyOS booted successfully.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "IDT initialized.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Kernel heap self-test passed.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Allocator integrity self-test passed.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Boot module metadata validated.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "ELF loader validation scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "RAMFS file tools scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Syscall argument validation scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Syscall boundary policy contract ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Syscall definition table ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Syscall filter policy ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Syscall resource limit policy ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Language runtime manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Application capability profile scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP package registry scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP trust store scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TAPP package verification scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Application launch policy scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "System management tools manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Secure image provisioning manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Device registry scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Block device scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Block VFS mount scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Framebuffer surface scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Renderer scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Renderer primitive scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Terminal UI scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Terminal panel scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TUI widget scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Window manager scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Desktop shell prototype ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Fullscreen desktop mode ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Desktop input interactions ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "TUI widget event bridge ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "UI event queue scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "System requirements manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Platform compatibility manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "PC platform initialization contract ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "PC required device classes ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Device RAMFS metadata scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Architecture capability manifest ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Address space scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Address space protection flag scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Kernel section protection contract ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Boot module address-space regions ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Address-space paging policy gap diagnostics ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Address-space paging policy applied to bootstrap tables.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Runtime paging enabled with protected bootstrap map.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Paging structures prepared for bootstrap identity map.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Paging protection flag scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "PIT IRQ0 stable at 100 Hz.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Keyboard IRQ1 enabled with polling fallback.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Kernel task stack ownership scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "i686 context switch ABI scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Scheduler scaffold receiving PIT ticks.");
    tinyos::api::print("Architecture: ");
    tinyos::api::print(tinyos::config::Architecture);
    tinyos::api::print("\n");
#if defined(TINYOS_DEBUG_BOOT)
    tinyos::api::print("Debug boot mode active.\n");
#endif
    tinyos::api::print("Type 'help' to list commands.\n\n");

    debug_boot_checkpoint("entering shell");
    tinyos::shell::run();
}
