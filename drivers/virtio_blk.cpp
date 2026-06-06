#include <tinyos/arch/pci.hpp>
#include <tinyos/drivers/virtio_blk.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/frame_allocator.hpp>
#include <tinyos/kernel/memory/paging.hpp>

namespace
{
    constexpr uint16_t VendorVirtio = 0x1AF4;
    constexpr uint16_t DeviceVirtioBlkLegacy = 0x1001;
    constexpr uint16_t DeviceVirtioBlkModern = 0x1042;
    constexpr uint8_t PciCapVendorSpecific = 0x09;
    constexpr uint8_t VirtioPciCfgCommon = 1;
    constexpr uint8_t VirtioPciCfgNotify = 2;
    constexpr uint8_t VirtioPciCfgDevice = 4;
    constexpr uint32_t VirtioMagic = 0x74726976u;
    constexpr uint32_t VirtioStatusAcknowledge = 1u;
    constexpr uint32_t VirtioStatusDriver = 2u;
    constexpr uint32_t VirtioStatusDriverOk = 4u;
    constexpr uint32_t VirtioStatusFeaturesOk = 8u;
    constexpr uint32_t VirtioStatusFailed = 128u;
    constexpr uint32_t VirtioBlkTIn = 0u;
    constexpr uint32_t VirtioBlkTOut = 1u;
    constexpr uint16_t VirtqDescFNext = 1u;
    constexpr uint16_t VirtqDescFWrite = 2u;
    constexpr size_t QueueSize = 4;
    constexpr size_t SectorSize = 512;
    constexpr uint32_t PollLimit = 10000000u;

    enum class Transport
    {
        None,
        LegacyMmio,
        ModernPci
    };

    struct VirtqDesc
    {
        uint64_t address;
        uint32_t length;
        uint16_t flags;
        uint16_t next;
    };

    struct VirtqAvail
    {
        uint16_t flags;
        uint16_t index;
        uint16_t ring[QueueSize];
    };

    struct VirtqUsedElem
    {
        uint32_t id;
        uint32_t length;
    };

    struct VirtqUsed
    {
        uint16_t flags;
        uint16_t index;
        VirtqUsedElem ring[QueueSize];
    };

    struct VirtioBlkRequest
    {
        uint32_t type;
        uint32_t reserved;
        uint64_t sector;
    };

    Transport g_transport = Transport::None;
    tinyos::arch::pci::DeviceLocation g_pci_location = {};
    volatile uint8_t* g_legacy_mmio = nullptr;
    volatile uint8_t* g_common_cfg = nullptr;
    volatile uint8_t* g_device_cfg = nullptr;
    volatile uint16_t* g_notify = nullptr;
    VirtqDesc* g_desc = nullptr;
    VirtqAvail* g_avail = nullptr;
    VirtqUsed* g_used = nullptr;
    VirtioBlkRequest* g_request = nullptr;
    uint8_t* g_data = nullptr;
    uint8_t* g_status = nullptr;
    uint16_t g_last_used_index = 0;
    uint8_t g_validation_write[SectorSize] = {};
    uint8_t g_validation_read[SectorSize] = {};
    tinyos::kernel::device::block::Device g_device = {};
    bool g_ready = false;

    void memory_barrier()
    {
        asm volatile ("" ::: "memory");
    }

    uint32_t read_u32(const volatile uint8_t* base, uint32_t offset)
    {
        const volatile uint32_t* words = reinterpret_cast<const volatile uint32_t*>(base + offset);
        return words[0];
    }

    void write_u32(volatile uint8_t* base, uint32_t offset, uint32_t value)
    {
        volatile uint32_t* words = reinterpret_cast<volatile uint32_t*>(base + offset);
        words[0] = value;
    }

    void write_u64(volatile uint8_t* base, uint32_t offset, uint64_t value)
    {
        write_u32(base, offset, static_cast<uint32_t>(value & 0xFFFFFFFFu));
        write_u32(base, offset + 4, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu));
    }

    uint8_t read_u8(const volatile uint8_t* base, uint32_t offset)
    {
        return base[offset];
    }

    void write_u8(volatile uint8_t* base, uint32_t offset, uint8_t value)
    {
        base[offset] = value;
    }

    void write_u16(volatile uint8_t* base, uint32_t offset, uint16_t value)
    {
        *reinterpret_cast<volatile uint16_t*>(base + offset) = value;
    }

    uint16_t read_u16(const volatile uint8_t* base, uint32_t offset)
    {
        return *reinterpret_cast<const volatile uint16_t*>(base + offset);
    }

    bool map_bar_window(uint8_t bar, uint32_t offset, uint32_t length, volatile uint8_t*& mapped_base)
    {
        const uintptr_t bar_phys = tinyos::arch::pci::bar_address(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, bar);
        if (bar_phys == 0)
        {
            return false;
        }

        const uintptr_t window = bar_phys + offset;
        size_t map_size = length == 0 ? tinyos::kernel::memory::frames::FrameSize : length;
        if (map_size < tinyos::kernel::memory::frames::FrameSize)
        {
            map_size = tinyos::kernel::memory::frames::FrameSize;
        }

        map_size = (map_size + tinyos::kernel::memory::frames::FrameSize - 1) & ~(tinyos::kernel::memory::frames::FrameSize - 1);
        const size_t mapped = tinyos::kernel::memory::paging::map_identity_range(
            window,
            map_size,
            tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite);
        if (mapped == 0)
        {
            return false;
        }

        mapped_base = reinterpret_cast<volatile uint8_t*>(window);
        return true;
    }

    void enable_pci_bus_master()
    {
        const uint32_t command = tinyos::arch::pci::config_read32(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, 0x04);
        tinyos::arch::pci::config_write32(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, 0x04, command | 0x0006u);
    }

    bool probe_modern_caps()
    {
        uint8_t cap_ptr = tinyos::arch::pci::config_read8(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, 0x34);
        bool have_common = false;
        bool have_device = false;
        bool have_notify = false;

        while (cap_ptr != 0)
        {
            const uint8_t cap_id = tinyos::arch::pci::config_read8(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, cap_ptr);
            if (cap_id == PciCapVendorSpecific)
            {
                const uint8_t cfg_type = tinyos::arch::pci::config_read8(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, static_cast<uint8_t>(cap_ptr + 3));
                const uint8_t bar = tinyos::arch::pci::config_read8(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, static_cast<uint8_t>(cap_ptr + 4));
                const uint32_t bar_offset = tinyos::arch::pci::config_read32(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, static_cast<uint8_t>(cap_ptr + 8));
                const uint32_t cap_length = tinyos::arch::pci::config_read32(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, static_cast<uint8_t>(cap_ptr + 12));

                if (cfg_type == VirtioPciCfgCommon && !have_common)
                {
                    have_common = map_bar_window(bar, bar_offset, cap_length, g_common_cfg);
                }
                else if (cfg_type == VirtioPciCfgDevice && !have_device)
                {
                    have_device = map_bar_window(bar, bar_offset, cap_length, g_device_cfg);
                }
                else if (cfg_type == VirtioPciCfgNotify && !have_notify)
                {
                    volatile uint8_t* notify_base = nullptr;
                    if (map_bar_window(bar, bar_offset, cap_length, notify_base))
                    {
                        g_notify = reinterpret_cast<volatile uint16_t*>(notify_base);
                        have_notify = true;
                    }
                }
            }

            cap_ptr = tinyos::arch::pci::config_read8(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, static_cast<uint8_t>(cap_ptr + 1));
        }

        if (have_common && have_device && have_notify)
        {
            g_transport = Transport::ModernPci;
            return true;
        }

        g_common_cfg = nullptr;
        g_device_cfg = nullptr;
        g_notify = nullptr;
        return false;
    }

    bool probe_legacy_bar()
    {
        const uintptr_t bar0 = tinyos::arch::pci::bar_address(g_pci_location.bus, g_pci_location.slot, g_pci_location.function, 0);
        if (bar0 == 0)
        {
            return false;
        }

        const size_t mapped = tinyos::kernel::memory::paging::map_identity_range(
            bar0,
            tinyos::kernel::memory::frames::FrameSize,
            tinyos::kernel::memory::paging::PageFlagRead | tinyos::kernel::memory::paging::PageFlagWrite);
        if (mapped == 0)
        {
            return false;
        }

        g_legacy_mmio = reinterpret_cast<volatile uint8_t*>(bar0);
        if (read_u32(g_legacy_mmio, 0x000) != VirtioMagic)
        {
            g_legacy_mmio = nullptr;
            return false;
        }

        g_transport = Transport::LegacyMmio;
        return true;
    }

    bool probe_pci()
    {
        if (!tinyos::arch::pci::find_device(VendorVirtio, DeviceVirtioBlkLegacy, g_pci_location) &&
            !tinyos::arch::pci::find_device(VendorVirtio, DeviceVirtioBlkModern, g_pci_location))
        {
            return false;
        }

        enable_pci_bus_master();
        return probe_modern_caps() || probe_legacy_bar();
    }

    bool setup_queue_memory()
    {
        const uintptr_t desc_frame = tinyos::kernel::memory::frames::allocate();
        const uintptr_t avail_frame = tinyos::kernel::memory::frames::allocate();
        const uintptr_t used_frame = tinyos::kernel::memory::frames::allocate();
        const uintptr_t request_frame = tinyos::kernel::memory::frames::allocate();
        const uintptr_t data_frame = tinyos::kernel::memory::frames::allocate();
        const uintptr_t status_frame = tinyos::kernel::memory::frames::allocate();
        if (desc_frame == 0 || avail_frame == 0 || used_frame == 0 || request_frame == 0 || data_frame == 0 || status_frame == 0)
        {
            return false;
        }

        g_desc = reinterpret_cast<VirtqDesc*>(desc_frame);
        g_avail = reinterpret_cast<VirtqAvail*>(avail_frame);
        g_used = reinterpret_cast<VirtqUsed*>(used_frame);
        g_request = reinterpret_cast<VirtioBlkRequest*>(request_frame);
        g_data = reinterpret_cast<uint8_t*>(data_frame);
        g_status = reinterpret_cast<uint8_t*>(status_frame);

        for (size_t index = 0; index < QueueSize; ++index)
        {
            g_desc[index].address = 0;
            g_desc[index].length = 0;
            g_desc[index].flags = 0;
            g_desc[index].next = 0;
            g_avail->ring[index] = 0;
            g_used->ring[index].id = 0;
            g_used->ring[index].length = 0;
        }

        g_avail->flags = 0;
        g_avail->index = 0;
        g_used->flags = 0;
        g_used->index = 0;
        g_last_used_index = 0;
        return true;
    }

    bool setup_queue_registers()
    {
        if (g_transport == Transport::LegacyMmio)
        {
            write_u32(g_legacy_mmio, 0x030, 0);
            const uint32_t queue_max = read_u32(g_legacy_mmio, 0x034);
            if (queue_max < QueueSize)
            {
                return false;
            }

            write_u32(g_legacy_mmio, 0x038, static_cast<uint32_t>(QueueSize));
            write_u64(g_legacy_mmio, 0x080, reinterpret_cast<uintptr_t>(g_desc));
            write_u64(g_legacy_mmio, 0x090, reinterpret_cast<uintptr_t>(g_avail));
            write_u64(g_legacy_mmio, 0x0A0, reinterpret_cast<uintptr_t>(g_used));
            write_u32(g_legacy_mmio, 0x044, 1);
            return read_u32(g_legacy_mmio, 0x044) == 1;
        }

        if (g_transport == Transport::ModernPci)
        {
            write_u16(g_common_cfg, 0x16, 0);
            const uint16_t queue_max = read_u16(g_common_cfg, 0x18);
            if (queue_max < QueueSize)
            {
                return false;
            }

            write_u16(g_common_cfg, 0x1A, 0xFFFF);
            write_u16(g_common_cfg, 0x18, static_cast<uint16_t>(QueueSize));
            write_u64(g_common_cfg, 0x20, reinterpret_cast<uintptr_t>(g_desc));
            write_u64(g_common_cfg, 0x28, reinterpret_cast<uintptr_t>(g_avail));
            write_u64(g_common_cfg, 0x30, reinterpret_cast<uintptr_t>(g_used));
            write_u16(g_common_cfg, 0x1C, 1);
            return read_u16(g_common_cfg, 0x1C) == 1;
        }

        return false;
    }

    bool start_device()
    {
        if (g_transport == Transport::None)
        {
            return false;
        }

        if (g_transport == Transport::LegacyMmio)
        {
            if (read_u32(g_legacy_mmio, 0x008) != 2)
            {
                return false;
            }

            write_u32(g_legacy_mmio, 0x070, 0);
            write_u32(g_legacy_mmio, 0x070, VirtioStatusAcknowledge);
            write_u32(g_legacy_mmio, 0x070, VirtioStatusAcknowledge | VirtioStatusDriver);
            if (read_u32(g_legacy_mmio, 0x004) == 1)
            {
                write_u32(g_legacy_mmio, 0x028, tinyos::kernel::memory::frames::FrameSize);
            }

            write_u32(g_legacy_mmio, 0x024, 0);
            write_u32(g_legacy_mmio, 0x020, 0);
            write_u32(g_legacy_mmio, 0x070, VirtioStatusAcknowledge | VirtioStatusDriver | VirtioStatusFeaturesOk);
        }
        if (g_transport == Transport::ModernPci)
        {
            write_u32(g_common_cfg, 0x00, 0);
            (void)read_u32(g_common_cfg, 0x04);
            write_u32(g_common_cfg, 0x08, 0);
            write_u32(g_common_cfg, 0x0C, 0);
            write_u8(g_common_cfg, 0x14, 0);
            write_u8(g_common_cfg, 0x14, VirtioStatusAcknowledge);
            write_u8(g_common_cfg, 0x14, static_cast<uint8_t>(VirtioStatusAcknowledge | VirtioStatusDriver));
            write_u8(g_common_cfg, 0x14, static_cast<uint8_t>(VirtioStatusAcknowledge | VirtioStatusDriver | VirtioStatusFeaturesOk));
        }

        if (!setup_queue_memory() || !setup_queue_registers())
        {
            if (g_transport == Transport::LegacyMmio)
            {
                write_u32(g_legacy_mmio, 0x070, VirtioStatusFailed);
            }
            else
            {
                write_u8(g_common_cfg, 0x14, VirtioStatusFailed);
            }

            return false;
        }

        if (g_transport == Transport::LegacyMmio)
        {
            write_u32(g_legacy_mmio, 0x070, VirtioStatusAcknowledge | VirtioStatusDriver | VirtioStatusFeaturesOk | VirtioStatusDriverOk);
            return (read_u32(g_legacy_mmio, 0x070) & VirtioStatusDriverOk) != 0;
        }

        write_u8(g_common_cfg, 0x14, static_cast<uint8_t>(VirtioStatusAcknowledge | VirtioStatusDriver | VirtioStatusFeaturesOk | VirtioStatusDriverOk));
        return (read_u8(g_common_cfg, 0x14) & VirtioStatusDriverOk) != 0;
    }

    uint64_t parse_volume_sector_count(const uint8_t* sector)
    {
        const char* marker = "sectors=";
        for (size_t start = 0; start + 8 < SectorSize; ++start)
        {
            size_t marker_index = 0;
            while (marker[marker_index] != '\0')
            {
                if (sector[start + marker_index] != static_cast<uint8_t>(marker[marker_index]))
                {
                    break;
                }

                ++marker_index;
            }

            if (marker[marker_index] != '\0')
            {
                continue;
            }

            uint64_t value = 0;
            size_t index = start + marker_index;
            while (index < SectorSize && sector[index] >= static_cast<uint8_t>('0') && sector[index] <= static_cast<uint8_t>('9'))
            {
                value = value * 10u + static_cast<uint64_t>(sector[index] - static_cast<uint8_t>('0'));
                ++index;
            }

            return value;
        }

        return 0;
    }

    uint64_t read_capacity_sectors()
    {
        if (g_transport == Transport::LegacyMmio)
        {
            const uint32_t low = read_u32(g_legacy_mmio, 0x100);
            const uint32_t high = read_u32(g_legacy_mmio, 0x104);
            return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
        }

        const uint32_t low = read_u32(g_device_cfg, 0x000);
        const uint32_t high = read_u32(g_device_cfg, 0x004);
        return (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
    }

    void notify_queue()
    {
        if (g_transport == Transport::LegacyMmio)
        {
            write_u32(g_legacy_mmio, 0x050, 0);
            return;
        }

        const uint16_t notify_offset = read_u16(g_common_cfg, 0x1E);
        g_notify[notify_offset] = 0;
    }

    bool submit_request(uint32_t type, uint64_t sector, const void* write_source, void* read_destination)
    {
        if (!g_ready || g_desc == nullptr || g_avail == nullptr || g_used == nullptr)
        {
            return false;
        }

        g_request->type = type;
        g_request->reserved = 0;
        g_request->sector = sector;
        *g_status = 0xFF;

        g_desc[0].address = reinterpret_cast<uintptr_t>(g_request);
        g_desc[0].length = sizeof(VirtioBlkRequest);
        g_desc[0].flags = VirtqDescFNext;
        g_desc[0].next = 1;

        g_desc[1].address = reinterpret_cast<uintptr_t>(type == VirtioBlkTOut ? write_source : g_data);
        g_desc[1].length = SectorSize;
        g_desc[1].flags = static_cast<uint16_t>(VirtqDescFNext | ((type == VirtioBlkTIn) ? VirtqDescFWrite : 0));
        g_desc[1].next = 2;

        g_desc[2].address = reinterpret_cast<uintptr_t>(g_status);
        g_desc[2].length = 1;
        g_desc[2].flags = VirtqDescFWrite;
        g_desc[2].next = 0;

        const uint16_t slot = g_avail->index % static_cast<uint16_t>(QueueSize);
        g_avail->ring[slot] = 0;
        memory_barrier();
        g_avail->index = static_cast<uint16_t>(g_avail->index + 1);
        memory_barrier();
        notify_queue();

        for (uint32_t spin = 0; spin < PollLimit; ++spin)
        {
            memory_barrier();
            if (g_used->index != g_last_used_index)
            {
                g_last_used_index = g_used->index;
                if (*g_status != 0)
                {
                    return false;
                }

                if (type == VirtioBlkTIn && read_destination != nullptr)
                {
                    auto* destination = static_cast<uint8_t*>(read_destination);
                    for (size_t index = 0; index < SectorSize; ++index)
                    {
                        destination[index] = g_data[index];
                    }
                }

                return true;
            }
        }

        return false;
    }
}

namespace tinyos::drivers::virtio_blk
{
    void initialize()
    {
        g_ready = false;
        g_transport = Transport::None;
        g_legacy_mmio = nullptr;
        g_common_cfg = nullptr;
        g_device_cfg = nullptr;
        g_notify = nullptr;

        if (!tinyos::kernel::memory::paging::is_runtime_enabled())
        {
            return;
        }

        if (!tinyos::arch::pci::find_device(VendorVirtio, DeviceVirtioBlkLegacy, g_pci_location) &&
            !tinyos::arch::pci::find_device(VendorVirtio, DeviceVirtioBlkModern, g_pci_location))
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "VirtIO PCI device not found.");
            return;
        }

        enable_pci_bus_master();
        if (!probe_modern_caps() && !probe_legacy_bar())
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "VirtIO PCI transport mapping failed.");
            return;
        }

        if (!start_device())
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "VirtIO PCI device start failed.");
            return;
        }

        const uint64_t capacity = read_capacity_sectors();
        if (capacity == 0)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "VirtIO block device reported zero capacity.");
            return;
        }

        uint64_t sector_count = capacity;
        g_device.name = "virtio-blk0";
        g_device.sector_size = static_cast<uint32_t>(SectorSize);
        g_device.sector_count = sector_count > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(sector_count);
        g_device.writable = true;
        g_device.ready = true;
        g_ready = true;

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_validation_read[index] = 0;
        }

        if (read_sector(0, g_validation_read, SectorSize) == tinyos::kernel::device::block::Status::Ok)
        {
            const uint64_t volume_sectors = parse_volume_sector_count(g_validation_read);
            if (volume_sectors > sector_count)
            {
                sector_count = volume_sectors;
                g_device.sector_count = sector_count > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(sector_count);
            }
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_validation_read[index] = 0;
        }

        if (read_sector(0, g_validation_read, SectorSize) != tinyos::kernel::device::block::Status::Ok || g_validation_read[0] == 0)
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "VirtIO block sector0 probe failed.");
            g_device.ready = false;
            g_ready = false;
            return;
        }

        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "VirtIO block device ready.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    const tinyos::kernel::device::block::Device* device()
    {
        return g_ready ? &g_device : nullptr;
    }

    tinyos::kernel::device::block::Status read_sector(uint32_t sector_index, void* buffer, size_t buffer_size)
    {
        if (!g_ready)
        {
            return tinyos::kernel::device::block::Status::NotReady;
        }

        if (buffer == nullptr)
        {
            return tinyos::kernel::device::block::Status::InvalidArgument;
        }

        if (sector_index >= g_device.sector_count)
        {
            return tinyos::kernel::device::block::Status::OutOfRange;
        }

        if (buffer_size < SectorSize)
        {
            return tinyos::kernel::device::block::Status::BufferTooSmall;
        }

        return submit_request(VirtioBlkTIn, sector_index, nullptr, buffer)
            ? tinyos::kernel::device::block::Status::Ok
            : tinyos::kernel::device::block::Status::NotReady;
    }

    tinyos::kernel::device::block::Status write_sector(uint32_t sector_index, const void* data, size_t data_size)
    {
        if (!g_ready)
        {
            return tinyos::kernel::device::block::Status::NotReady;
        }

        if (data == nullptr)
        {
            return tinyos::kernel::device::block::Status::InvalidArgument;
        }

        if (sector_index >= g_device.sector_count)
        {
            return tinyos::kernel::device::block::Status::OutOfRange;
        }

        if (data_size < SectorSize)
        {
            return tinyos::kernel::device::block::Status::BufferTooSmall;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_data[index] = static_cast<const uint8_t*>(data)[index];
        }

        return submit_request(VirtioBlkTOut, sector_index, g_data, nullptr)
            ? tinyos::kernel::device::block::Status::Ok
            : tinyos::kernel::device::block::Status::NotReady;
    }

    bool validation_self_test()
    {
        if (!g_ready || g_device.sector_count == 0)
        {
            return false;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_validation_write[index] = static_cast<uint8_t>(0xA0u + (index & 0x3Fu));
            g_validation_read[index] = 0;
        }

        if (read_sector(0, g_validation_read, sizeof(g_validation_read)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        if (g_validation_read[0] == 0)
        {
            return false;
        }

        if (g_device.sector_count <= 1)
        {
            return false;
        }

        const uint32_t test_sector = g_device.sector_count - 1;
        if (write_sector(test_sector, g_validation_write, sizeof(g_validation_write)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        if (read_sector(test_sector, g_validation_read, sizeof(g_validation_read)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            if (g_validation_read[index] != g_validation_write[index])
            {
                return false;
            }
        }

        return read_sector(g_device.sector_count, g_validation_read, sizeof(g_validation_read)) == tinyos::kernel::device::block::Status::OutOfRange;
    }
}
