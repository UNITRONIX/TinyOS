#pragma once

#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/device/block.hpp>

namespace tinyos::drivers::virtio_blk
{
    void initialize();
    bool is_ready();
    const tinyos::kernel::device::block::Device* device();
    tinyos::kernel::device::block::Status read_sector(uint32_t sector_index, void* buffer, size_t buffer_size);
    tinyos::kernel::device::block::Status write_sector(uint32_t sector_index, const void* data, size_t data_size);
    bool validation_self_test();
}
