#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::kernel::device::block
{
    enum class Status : uint32_t
    {
        Ok,
        NotReady,
        InvalidArgument,
        OutOfRange,
        BufferTooSmall,
        ReadOnly
    };

    struct Device
    {
        const char* name;
        uint32_t sector_size;
        uint32_t sector_count;
        bool writable;
        bool ready;
    };

    void initialize();
    bool is_ready();
    bool virtio_available();
    bool ata_available();
    const char* active_device_name();
    const Device* root_device();
    const Device* ram_device();
    uint32_t sector_size();
    uint32_t sector_count();
    size_t total_size();
    Status read_sector(uint32_t sector_index, void* buffer, size_t buffer_size);
    Status write_sector(uint32_t sector_index, const void* data, size_t data_size);
    Status write_bytes(uint32_t sector_index, const char* data, size_t data_size);
    bool validation_self_test();
    const char* status_name(Status status);
}