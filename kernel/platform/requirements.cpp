#include <tinyos/kernel/platform/requirements.hpp>

namespace
{
    const tinyos::kernel::platform::requirements::MinimumRequirements g_requirements = {
        "i686-compatible 32-bit CPU",
        "Multiboot via GRUB ISO",
        "BIOS-style PC boot path in QEMU",
        32,
        128,
        "VGA text mode, 80x25",
        "PS/2 keyboard controller",
        "8253/8254 PIT at 100 Hz",
        "8259 PIC",
        "ISO boot plus RAM-backed block scaffold",
        "qemu-system-i386"
    };
}

namespace tinyos::kernel::platform::requirements
{
    const MinimumRequirements& current()
    {
        return g_requirements;
    }

    bool validation_self_test()
    {
        return g_requirements.architecture != nullptr &&
            g_requirements.boot_protocol != nullptr &&
            g_requirements.minimum_memory_mib != 0 &&
            g_requirements.recommended_memory_mib >= g_requirements.minimum_memory_mib &&
            g_requirements.display != nullptr &&
            g_requirements.input != nullptr &&
            g_requirements.timer != nullptr &&
            g_requirements.interrupt_controller != nullptr &&
            g_requirements.storage != nullptr &&
            g_requirements.emulator != nullptr;
    }
}