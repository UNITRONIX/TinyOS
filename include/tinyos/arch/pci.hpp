#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::arch::pci
{
    struct DeviceLocation
    {
        uint8_t bus;
        uint8_t slot;
        uint8_t function;
    };

    struct DeviceId
    {
        uint16_t vendor;
        uint16_t device;
    };

    uint32_t config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
    void config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
    uint8_t config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
    void config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
    bool find_device(uint16_t vendor, uint16_t device, DeviceLocation& location);
    uintptr_t bar_address(uint8_t bus, uint8_t slot, uint8_t function, size_t bar_index);
}
