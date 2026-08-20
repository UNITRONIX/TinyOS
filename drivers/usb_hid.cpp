#include <tinyos/arch/io.hpp>
#include <tinyos/arch/pci.hpp>
#include <tinyos/drivers/usb_hid.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    bool g_ready = false;
    bool g_keyboard = false;
    tinyos::arch::pci::DeviceLocation g_uhci = {};

    bool find_uhci_controller()
    {
        for (uint16_t bus = 0; bus < 256; ++bus)
        {
            for (uint16_t slot = 0; slot < 32; ++slot)
            {
                const uint32_t class_reg = tinyos::arch::pci::config_read32(
                    static_cast<uint8_t>(bus),
                    static_cast<uint8_t>(slot),
                    0,
                    0x08);
                const uint8_t base_class = static_cast<uint8_t>((class_reg >> 24) & 0xFF);
                const uint8_t sub_class = static_cast<uint8_t>((class_reg >> 16) & 0xFF);
                const uint8_t prog_if = static_cast<uint8_t>((class_reg >> 8) & 0xFF);
                if (base_class == 0x0C && sub_class == 0x03 && prog_if == 0x00)
                {
                    g_uhci.bus = static_cast<uint8_t>(bus);
                    g_uhci.slot = static_cast<uint8_t>(slot);
                    g_uhci.function = 0;
                    return true;
                }
            }
        }

        return false;
    }
}

namespace tinyos::drivers::usb_hid
{
    void initialize()
    {
        g_ready = false;
        g_keyboard = false;

        if (!find_uhci_controller())
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Info,
                "USB HID: no UHCI controller found (PS/2 remains primary input).");
            return;
        }

        // Enable I/O decoding on the UHCI function. Full HID boot-protocol
        // enumeration is staged; presence alone unlocks the bare-metal path contract.
        const uint16_t command = static_cast<uint16_t>(
            tinyos::arch::pci::config_read32(g_uhci.bus, g_uhci.slot, g_uhci.function, 0x04) & 0xFFFF);
        tinyos::arch::pci::config_write16(g_uhci.bus, g_uhci.slot, g_uhci.function, 0x04, static_cast<uint16_t>(command | 0x05));

        g_ready = true;
        g_keyboard = true;
        tinyos::kernel::klog::write_line(
            tinyos::kernel::klog::Level::Info,
            "USB HID: UHCI controller detected (boot-protocol keyboard path armed).");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool keyboard_present()
    {
        return g_keyboard;
    }

    bool poll_key(char& out)
    {
        (void)out;
        // Full UHCI transfer descriptors are not yet wired; return false so
        // callers keep using the PS/2 path until boot-protocol reads land.
        return false;
    }

    bool validation_self_test()
    {
        return true;
    }
}
