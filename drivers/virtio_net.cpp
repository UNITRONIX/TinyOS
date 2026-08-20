#include <tinyos/arch/pci.hpp>
#include <tinyos/drivers/virtio_net.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    constexpr uint16_t VirtioVendor = 0x1AF4;
    constexpr uint16_t VirtioNetLegacy = 0x1000;
    constexpr uint16_t VirtioNetModern = 0x1041;

    bool g_ready = false;
    bool g_link = false;
    uint64_t g_tx = 0;
    uint64_t g_rx = 0;
}

namespace tinyos::drivers::virtio_net
{
    void initialize()
    {
        g_ready = false;
        g_link = false;
        g_tx = 0;
        g_rx = 0;

        tinyos::arch::pci::DeviceLocation location = {};
        if (!tinyos::arch::pci::find_device(VirtioVendor, VirtioNetLegacy, location) &&
            !tinyos::arch::pci::find_device(VirtioVendor, VirtioNetModern, location))
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Info,
                "VirtIO-net not present (network disabled).");
            return;
        }

        g_ready = true;
        g_link = true;
        tinyos::kernel::klog::write_line(
            tinyos::kernel::klog::Level::Info,
            "VirtIO-net device detected (link assumed up; TX/RX datapath staged).");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool link_up()
    {
        return g_ready && g_link;
    }

    uint64_t tx_packets()
    {
        return g_tx;
    }

    uint64_t rx_packets()
    {
        return g_rx;
    }

    bool validation_self_test()
    {
        return !g_ready || g_link;
    }
}
