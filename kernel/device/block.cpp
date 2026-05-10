#include <tinyos/kernel/device/block.hpp>

namespace
{
    constexpr uint32_t SectorSize = 512;
    constexpr uint32_t SectorCount = 8;
    constexpr size_t StorageSize = static_cast<size_t>(SectorSize) * SectorCount;
    constexpr char BootVolumeText[] = "TinyOS block volume\nname=ram-block0\nmode=read-only-vfs\nsector-size=512\nsectors=8\n";

    uint8_t g_storage[StorageSize] = {};
    uint8_t g_seed_sector[SectorSize] = {};
    uint8_t g_validation_write[SectorSize] = {};
    uint8_t g_validation_read[SectorSize] = {};
    tinyos::kernel::device::block::Device g_device = {};
    bool g_ready = false;

    size_t sector_offset(uint32_t sector_index)
    {
        return static_cast<size_t>(sector_index) * SectorSize;
    }
}

namespace tinyos::kernel::device::block
{
    void initialize()
    {
        for (size_t index = 0; index < StorageSize; ++index)
        {
            g_storage[index] = 0;
        }

        g_device.name = "ram-block0";
        g_device.sector_size = SectorSize;
        g_device.sector_count = SectorCount;
        g_device.writable = true;
        g_device.ready = true;
        g_ready = true;

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_seed_sector[index] = 0;
        }

        for (size_t index = 0; index + 1 < sizeof(BootVolumeText) && index < SectorSize; ++index)
        {
            g_seed_sector[index] = static_cast<uint8_t>(BootVolumeText[index]);
        }

        write_sector(0, g_seed_sector, SectorSize);
    }

    bool is_ready()
    {
        return g_ready && g_device.ready;
    }

    const Device* root_device()
    {
        return is_ready() ? &g_device : nullptr;
    }

    uint32_t sector_size()
    {
        return SectorSize;
    }

    uint32_t sector_count()
    {
        return SectorCount;
    }

    size_t total_size()
    {
        return StorageSize;
    }

    Status read_sector(uint32_t sector_index, void* buffer, size_t buffer_size)
    {
        if (!is_ready())
        {
            return Status::NotReady;
        }

        if (buffer == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= SectorCount)
        {
            return Status::OutOfRange;
        }

        if (buffer_size < SectorSize)
        {
            return Status::BufferTooSmall;
        }

        auto* destination = static_cast<uint8_t*>(buffer);
        const size_t offset = sector_offset(sector_index);
        for (size_t index = 0; index < SectorSize; ++index)
        {
            destination[index] = g_storage[offset + index];
        }

        return Status::Ok;
    }

    Status write_sector(uint32_t sector_index, const void* data, size_t data_size)
    {
        if (!is_ready())
        {
            return Status::NotReady;
        }

        if (!g_device.writable)
        {
            return Status::ReadOnly;
        }

        if (data == nullptr)
        {
            return Status::InvalidArgument;
        }

        if (sector_index >= SectorCount)
        {
            return Status::OutOfRange;
        }

        if (data_size < SectorSize)
        {
            return Status::BufferTooSmall;
        }

        const auto* source = static_cast<const uint8_t*>(data);
        const size_t offset = sector_offset(sector_index);
        for (size_t index = 0; index < SectorSize; ++index)
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

        if (data_size > SectorSize)
        {
            return Status::BufferTooSmall;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_seed_sector[index] = 0;
        }

        for (size_t index = 0; index < data_size; ++index)
        {
            g_seed_sector[index] = static_cast<uint8_t>(data[index]);
        }

        return write_sector(sector_index, g_seed_sector, SectorSize);
    }

    bool validation_self_test()
    {
        if (!is_ready())
        {
            return false;
        }

        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_validation_write[index] = static_cast<uint8_t>(index & 0xFF);
            g_validation_read[index] = 0;
        }

        const uint32_t test_sector = SectorCount - 1;
        if (write_sector(test_sector, g_validation_write, SectorSize) != Status::Ok)
        {
            return false;
        }

        if (read_sector(test_sector, g_validation_read, SectorSize) != Status::Ok)
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

        const size_t offset = sector_offset(test_sector);
        for (size_t index = 0; index < SectorSize; ++index)
        {
            g_storage[offset + index] = 0;
        }

        return read_sector(SectorCount, g_validation_read, SectorSize) == Status::OutOfRange;
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