#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/ata.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    constexpr uint16_t PrimaryIoBase = 0x1F0;
    constexpr uint16_t PrimaryControlBase = 0x3F6;
    constexpr uint16_t DataPort = PrimaryIoBase + 0;
    constexpr uint16_t ErrorPort = PrimaryIoBase + 1;
    constexpr uint16_t SectorCountPort = PrimaryIoBase + 2;
    constexpr uint16_t LbaLowPort = PrimaryIoBase + 3;
    constexpr uint16_t LbaMidPort = PrimaryIoBase + 4;
    constexpr uint16_t LbaHighPort = PrimaryIoBase + 5;
    constexpr uint16_t DrivePort = PrimaryIoBase + 6;
    constexpr uint16_t StatusPort = PrimaryIoBase + 7;
    constexpr uint16_t CommandPort = PrimaryIoBase + 7;

    constexpr uint8_t StatusError = 0x01;
    constexpr uint8_t StatusDataRequest = 0x08;
    constexpr uint8_t StatusBusy = 0x80;
    constexpr uint8_t StatusReady = 0x40;

    constexpr uint8_t CommandIdentify = 0xEC;
    constexpr uint8_t CommandReadSectors = 0x20;
    constexpr uint8_t CommandWriteSectors = 0x30;
    constexpr uint8_t CommandCacheFlush = 0xE7;

    constexpr uint32_t SectorSize = 512;
    constexpr uint32_t MaxWaitLoops = 500000;

    tinyos::kernel::device::block::Device g_device = {};
    uint16_t g_identify[256] = {};
    bool g_ready = false;

    bool wait_not_busy()
    {
        for (uint32_t attempt = 0; attempt < MaxWaitLoops; ++attempt)
        {
            const uint8_t status = tinyos::arch::io::inb(StatusPort);
            if ((status & StatusBusy) == 0)
            {
                return true;
            }
        }

        return false;
    }

    bool wait_drq()
    {
        for (uint32_t attempt = 0; attempt < MaxWaitLoops; ++attempt)
        {
            const uint8_t status = tinyos::arch::io::inb(StatusPort);
            if ((status & StatusError) != 0)
            {
                return false;
            }

            if ((status & StatusDataRequest) != 0)
            {
                return true;
            }
        }

        return false;
    }

    void select_drive_lba(uint32_t lba)
    {
        tinyos::arch::io::outb(DrivePort, static_cast<uint8_t>(0xE0 | ((lba >> 24) & 0x0F)));
        tinyos::arch::io::io_wait();
    }

    bool identify_drive()
    {
        select_drive_lba(0);
        tinyos::arch::io::outb(SectorCountPort, 0);
        tinyos::arch::io::outb(LbaLowPort, 0);
        tinyos::arch::io::outb(LbaMidPort, 0);
        tinyos::arch::io::outb(LbaHighPort, 0);
        tinyos::arch::io::outb(CommandPort, CommandIdentify);

        uint8_t status = tinyos::arch::io::inb(StatusPort);
        if (status == 0)
        {
            return false;
        }

        if (!wait_not_busy())
        {
            return false;
        }

        // Floating bus / no device: LBA mid/high stay non-zero for ATAPI.
        if (tinyos::arch::io::inb(LbaMidPort) != 0 || tinyos::arch::io::inb(LbaHighPort) != 0)
        {
            return false;
        }

        if (!wait_drq())
        {
            return false;
        }

        for (size_t index = 0; index < 256; ++index)
        {
            g_identify[index] = tinyos::arch::io::inw(DataPort);
        }

        uint32_t sectors = g_identify[60] | (static_cast<uint32_t>(g_identify[61]) << 16);
        if (sectors == 0)
        {
            sectors = g_identify[57] | (static_cast<uint32_t>(g_identify[58]) << 16);
        }

        if (sectors == 0)
        {
            return false;
        }

        g_device.name = "ata0-master";
        g_device.sector_size = SectorSize;
        g_device.sector_count = sectors;
        g_device.writable = true;
        g_device.ready = true;
        return true;
    }

    bool issue_lba28(uint8_t command, uint32_t lba)
    {
        if (!wait_not_busy())
        {
            return false;
        }

        select_drive_lba(lba);
        tinyos::arch::io::outb(SectorCountPort, 1);
        tinyos::arch::io::outb(LbaLowPort, static_cast<uint8_t>(lba & 0xFF));
        tinyos::arch::io::outb(LbaMidPort, static_cast<uint8_t>((lba >> 8) & 0xFF));
        tinyos::arch::io::outb(LbaHighPort, static_cast<uint8_t>((lba >> 16) & 0xFF));
        tinyos::arch::io::outb(CommandPort, command);
        return wait_drq();
    }
}

namespace tinyos::drivers::ata
{
    void initialize()
    {
        g_ready = false;
        g_device = {};

        // Soft reset primary channel; ignore result on empty bus.
        tinyos::arch::io::outb(PrimaryControlBase, 0x04);
        tinyos::arch::io::io_wait();
        tinyos::arch::io::outb(PrimaryControlBase, 0x00);
        tinyos::arch::io::io_wait();

        if (!identify_drive())
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "ATA primary master not present.");
            return;
        }

        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "ATA PIO primary master ready.");
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
        using Status = tinyos::kernel::device::block::Status;
        if (!g_ready)
        {
            return Status::NotReady;
        }

        if (buffer == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= g_device.sector_count)
        {
            return Status::OutOfRange;
        }

        if (buffer_size < SectorSize)
        {
            return Status::BufferTooSmall;
        }

        if (!issue_lba28(CommandReadSectors, sector_index))
        {
            return Status::NotReady;
        }

        auto* destination = static_cast<uint16_t*>(buffer);
        for (size_t index = 0; index < SectorSize / 2; ++index)
        {
            destination[index] = tinyos::arch::io::inw(DataPort);
        }

        return Status::Ok;
    }

    tinyos::kernel::device::block::Status write_sector(uint32_t sector_index, const void* data, size_t data_size)
    {
        using Status = tinyos::kernel::device::block::Status;
        if (!g_ready)
        {
            return Status::NotReady;
        }

        if (data == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= g_device.sector_count)
        {
            return Status::OutOfRange;
        }

        if (data_size < SectorSize)
        {
            return Status::BufferTooSmall;
        }

        if (!issue_lba28(CommandWriteSectors, sector_index))
        {
            return Status::NotReady;
        }

        const auto* source = static_cast<const uint16_t*>(data);
        for (size_t index = 0; index < SectorSize / 2; ++index)
        {
            tinyos::arch::io::outw(DataPort, source[index]);
        }

        if (!wait_not_busy())
        {
            return Status::NotReady;
        }

        tinyos::arch::io::outb(CommandPort, CommandCacheFlush);
        wait_not_busy();
        return Status::Ok;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return true;
        }

        uint8_t write_buffer[SectorSize] = {};
        uint8_t read_buffer[SectorSize] = {};
        for (size_t index = 0; index < SectorSize; ++index)
        {
            write_buffer[index] = static_cast<uint8_t>(0xA5 ^ (index & 0xFF));
        }

        // Use the last sector as a scratch pad so we do not disturb boot/FAT metadata.
        const uint32_t test_sector = g_device.sector_count > 1 ? g_device.sector_count - 1 : 0;
        if (write_sector(test_sector, write_buffer, SectorSize) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        if (read_sector(test_sector, read_buffer, SectorSize) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            if (read_buffer[index] != write_buffer[index])
            {
                return false;
            }
        }

        return true;
    }
}
