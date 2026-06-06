#include <tinyos/arch/io.hpp>
#include <tinyos/arch/pci.hpp>

namespace
{
    constexpr uint16_t ConfigAddressPort = 0xCF8;
    constexpr uint16_t ConfigDataPort = 0xCFC;

    uint32_t make_config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
    {
        return 0x80000000u |
            (static_cast<uint32_t>(bus) << 16) |
            (static_cast<uint32_t>(slot) << 11) |
            (static_cast<uint32_t>(function) << 8) |
            (static_cast<uint32_t>(offset) & 0xFCu);
    }
}

namespace tinyos::arch::pci
{
    uint32_t config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
    {
        io::outl(ConfigAddressPort, make_config_address(bus, slot, function, offset));
        return io::inl(ConfigDataPort);
    }

    void config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value)
    {
        io::outl(ConfigAddressPort, make_config_address(bus, slot, function, offset));
        io::outl(ConfigDataPort, value);
    }

    uint8_t config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
    {
        const uint32_t value = config_read32(bus, slot, function, static_cast<uint8_t>(offset & 0xFCu));
        const uint32_t shift = (static_cast<uint32_t>(offset & 0x3u) * 8u);
        return static_cast<uint8_t>((value >> shift) & 0xFFu);
    }

    void config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value)
    {
        const uint8_t aligned = static_cast<uint8_t>(offset & 0xFCu);
        uint32_t current = config_read32(bus, slot, function, aligned);
        if ((offset & 2u) != 0)
        {
            current = (current & 0x0000FFFFu) | (static_cast<uint32_t>(value) << 16);
        }
        else
        {
            current = (current & 0xFFFF0000u) | static_cast<uint32_t>(value);
        }

        config_write32(bus, slot, function, aligned, current);
    }

    bool find_device(uint16_t vendor, uint16_t device, DeviceLocation& location)
    {
        for (uint16_t bus = 0; bus < 256; ++bus)
        {
            for (uint16_t slot = 0; slot < 32; ++slot)
            {
                const uint32_t header = config_read32(static_cast<uint8_t>(bus), static_cast<uint8_t>(slot), 0, 0);
                const uint16_t header_vendor = static_cast<uint16_t>(header & 0xFFFFu);
                if (header_vendor == 0xFFFFu || header_vendor == 0x0000u)
                {
                    continue;
                }

                const uint16_t header_device = static_cast<uint16_t>((header >> 16) & 0xFFFFu);
                if (header_vendor == vendor && header_device == device)
                {
                    location.bus = static_cast<uint8_t>(bus);
                    location.slot = static_cast<uint8_t>(slot);
                    location.function = 0;
                    return true;
                }
            }
        }

        return false;
    }

    uintptr_t bar_address(uint8_t bus, uint8_t slot, uint8_t function, size_t bar_index)
    {
        if (bar_index > 5)
        {
            return 0;
        }

        const uint8_t offset = static_cast<uint8_t>(0x10 + (bar_index * 4));
        const uint32_t bar = config_read32(bus, slot, function, offset);
        if ((bar & 0x1u) != 0)
        {
            return 0;
        }

        return static_cast<uintptr_t>(bar & 0xFFFFFFF0u);
    }
}
