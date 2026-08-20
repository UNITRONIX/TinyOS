#include <tinyos/drivers/ata.hpp>
#include <tinyos/drivers/virtio_blk.hpp>
#include <tinyos/kernel/device/block.hpp>

namespace
{
    constexpr uint32_t RamSectorSize = 512;
    constexpr uint32_t RamSectorCount = 8;
    constexpr size_t RamStorageSize = static_cast<size_t>(RamSectorSize) * RamSectorCount;
    constexpr char BootVolumeText[] = "TinyOS block volume\nname=ram-block0\nmode=read-only-vfs\nsector-size=512\nsectors=8\n";

    uint8_t g_storage[RamStorageSize] = {};
    uint8_t g_seed_sector[RamSectorSize] = {};
    uint8_t g_validation_write[RamSectorSize] = {};
    uint8_t g_validation_read[RamSectorSize] = {};
    tinyos::kernel::device::block::Device g_ram_device = {};
    bool g_ram_ready = false;
    bool g_use_virtio = false;
    bool g_use_ata = false;

    size_t sector_offset(uint32_t sector_index)
    {
        return static_cast<size_t>(sector_index) * RamSectorSize;
    }

    void initialize_ram_backend()
    {
        for (size_t index = 0; index < RamStorageSize; ++index)
        {
            g_storage[index] = 0;
        }

        g_ram_device.name = "ram-block0";
        g_ram_device.sector_size = RamSectorSize;
        g_ram_device.sector_count = RamSectorCount;
        g_ram_device.writable = true;
        g_ram_device.ready = true;
        g_ram_ready = true;

        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_seed_sector[index] = 0;
        }

        for (size_t index = 0; index + 1 < sizeof(BootVolumeText) && index < RamSectorSize; ++index)
        {
            g_seed_sector[index] = static_cast<uint8_t>(BootVolumeText[index]);
        }

        const size_t offset = sector_offset(0);
        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_storage[offset + index] = g_seed_sector[index];
        }
    }

    bool ram_validation_self_test()
    {
        if (!g_ram_ready)
        {
            return false;
        }

        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_validation_write[index] = static_cast<uint8_t>(index & 0xFF);
            g_validation_read[index] = 0;
        }

        const uint32_t test_sector = RamSectorCount - 1;
        const size_t offset = sector_offset(test_sector);
        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_storage[offset + index] = g_validation_write[index];
        }

        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_validation_read[index] = g_storage[offset + index];
        }

        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            if (g_validation_read[index] != g_validation_write[index])
            {
                return false;
            }
        }

        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_storage[offset + index] = 0;
        }

        return true;
    }
}

namespace tinyos::kernel::device::block
{
    void initialize()
    {
        g_use_virtio = false;
        g_use_ata = false;
        initialize_ram_backend();
        g_use_virtio = tinyos::drivers::virtio_blk::is_ready();
        // ATA is used by FAT16 (/mnt/fat) directly. Do not promote it to the
        // TinyOS blockfs root volume — a blank or FAT-formatted sector 0 is not
        // the ram/virtio catalog text that blockfs expects.
        g_use_ata = tinyos::drivers::ata::is_ready();
    }

    bool is_ready()
    {
        if (g_use_virtio)
        {
            return tinyos::drivers::virtio_blk::is_ready();
        }

        return g_ram_ready;
    }

    bool virtio_available()
    {
        return g_use_virtio && tinyos::drivers::virtio_blk::is_ready();
    }

    bool ata_available()
    {
        return g_use_ata && tinyos::drivers::ata::is_ready();
    }

    const char* active_device_name()
    {
        if (virtio_available())
        {
            const auto* device = tinyos::drivers::virtio_blk::device();
            return device != nullptr && device->name != nullptr ? device->name : "virtio-blk0";
        }

        return g_ram_device.name;
    }

    const Device* root_device()
    {
        if (virtio_available())
        {
            return tinyos::drivers::virtio_blk::device();
        }

        return g_ram_ready ? &g_ram_device : nullptr;
    }

    const Device* ram_device()
    {
        return g_ram_ready ? &g_ram_device : nullptr;
    }

    uint32_t sector_size()
    {
        const auto* device = root_device();
        return device != nullptr ? device->sector_size : 0;
    }

    uint32_t sector_count()
    {
        const auto* device = root_device();
        return device != nullptr ? device->sector_count : 0;
    }

    size_t total_size()
    {
        const auto* device = root_device();
        return device != nullptr ? static_cast<size_t>(device->sector_size) * static_cast<size_t>(device->sector_count) : 0;
    }

    Status read_sector(uint32_t sector_index, void* buffer, size_t buffer_size)
    {
        if (virtio_available())
        {
            return tinyos::drivers::virtio_blk::read_sector(sector_index, buffer, buffer_size);
        }

        if (!g_ram_ready)
        {
            return Status::NotReady;
        }

        if (buffer == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= RamSectorCount)
        {
            return Status::OutOfRange;
        }

        if (buffer_size < RamSectorSize)
        {
            return Status::BufferTooSmall;
        }

        auto* destination = static_cast<uint8_t*>(buffer);
        const size_t offset = sector_offset(sector_index);
        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            destination[index] = g_storage[offset + index];
        }

        return Status::Ok;
    }

    Status write_sector(uint32_t sector_index, const void* data, size_t data_size)
    {
        if (virtio_available())
        {
            return tinyos::drivers::virtio_blk::write_sector(sector_index, data, data_size);
        }

        if (!g_ram_ready)
        {
            return Status::NotReady;
        }

        if (!g_ram_device.writable)
        {
            return Status::ReadOnly;
        }

        if (data == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= RamSectorCount)
        {
            return Status::OutOfRange;
        }

        if (data_size < RamSectorSize)
        {
            return Status::BufferTooSmall;
        }

        const auto* source = static_cast<const uint8_t*>(data);
        const size_t offset = sector_offset(sector_index);
        for (size_t index = 0; index < RamSectorSize; ++index)
        {
            g_storage[offset + index] = source[index];
        }

        return Status::Ok;
    }

    Status write_bytes(uint32_t sector_index, const char* data, size_t data_size)
    {
        if (data == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (data_size > sector_size())
        {
            return Status::BufferTooSmall;
        }

        for (size_t index = 0; index < sector_size(); ++index)
        {
            g_seed_sector[index] = 0;
        }

        for (size_t index = 0; index < data_size; ++index)
        {
            g_seed_sector[index] = static_cast<uint8_t>(data[index]);
        }

        return write_sector(sector_index, g_seed_sector, sector_size());
    }

    bool validation_self_test()
    {
        if (virtio_available())
        {
            return tinyos::drivers::virtio_blk::validation_self_test();
        }

        return ram_validation_self_test();
    }

    const char* status_name(Status status)
    {
        switch (status)
        {
        case Status::Ok:
            return "ok";
        case Status::NotReady:
            return "not-ready";
        case Status::InvalidArgument:
            return "invalid-argument";
        case Status::OutOfRange:
            return "out-of-range";
        case Status::BufferTooSmall:
            return "buffer-too-small";
        case Status::ReadOnly:
            return "read-only";
        }

        return "unknown";
    }
}
