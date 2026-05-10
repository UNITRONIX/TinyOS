#include <stdint.h>

#include <tinyos/kernel/device/block.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>

namespace
{
    constexpr char MountPath[] = "/volumes";
    constexpr char DeviceName[] = "ram-block0";
    constexpr char DeviceInfoText[] = "name=ram-block0\nclass=block\nmounted-at=/volumes/ram-block0\nmode=read-only\n";
    constexpr size_t SectorBufferSize = 512;

    uint8_t g_sector_buffer[SectorBufferSize] = {};
    char g_volume_text[SectorBufferSize + 1] = {};
    bool g_ready = false;

    tinyos::kernel::vfs::Node g_volumes = { "volumes", true, nullptr, nullptr, 0, 0, false, nullptr };
    tinyos::kernel::vfs::Node g_ram_block = { "ram-block0", true, nullptr, nullptr, 0, 0, false, &g_volumes };
    tinyos::kernel::vfs::Node g_device_info = { "device.txt", false, DeviceInfoText, nullptr, sizeof(DeviceInfoText) - 1, 0, false, &g_ram_block };
    tinyos::kernel::vfs::Node g_volume_info = { "volume.txt", false, g_volume_text, nullptr, 0, 0, false, &g_ram_block };

    tinyos::kernel::vfs::Node* g_nodes[] = {
        &g_volumes,
        &g_ram_block,
        &g_device_info,
        &g_volume_info
    };

    bool name_equals_segment(const char* name, const char* segment, size_t length)
    {
        size_t index = 0;
        while (index < length && name[index] != '\0')
        {
            if (name[index] != segment[index])
            {
                return false;
            }

            ++index;
        }

        return index == length && name[index] == '\0';
    }

    tinyos::kernel::vfs::Node* child_by_segment(tinyos::kernel::vfs::Node* parent, const char* segment, size_t length)
    {
        if (parent == nullptr || !parent->directory)
        {
            return nullptr;
        }

        for (size_t index = 0; index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++index)
        {
            auto* node = g_nodes[index];
            if (node->parent == parent && name_equals_segment(node->name, segment, length))
            {
                return node;
            }
        }

        return nullptr;
    }

    bool refresh_volume_text()
    {
        if (tinyos::kernel::device::block::read_sector(0, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            g_volume_info.size = 0;
            g_volume_text[0] = '\0';
            return false;
        }

        size_t size = 0;
        while (size < SectorBufferSize && g_sector_buffer[size] != 0)
        {
            g_volume_text[size] = static_cast<char>(g_sector_buffer[size]);
            ++size;
        }

        g_volume_text[size] = '\0';
        g_volume_info.size = size;
        return size != 0;
    }
}

namespace tinyos::kernel::vfs::blockfs
{
    void initialize()
    {
        g_ready = tinyos::kernel::device::block::is_ready() && refresh_volume_text();
        if (g_ready)
        {
            kernel::klog::write_line(kernel::klog::Level::Info, "Block VFS scaffold initialized.");
        }
    }

    bool is_ready()
    {
        return g_ready;
    }

    const Node* root()
    {
        return g_ready ? &g_volumes : nullptr;
    }

    const Node* find(const char* path)
    {
        if (!g_ready || path == nullptr)
        {
            return nullptr;
        }

        const char* cursor = path;
        while (*cursor == '/')
        {
            ++cursor;
        }

        const char* segment = cursor;
        size_t length = 0;
        while (cursor[length] != '\0' && cursor[length] != '/')
        {
            ++length;
        }

        if (!name_equals_segment(g_volumes.name, segment, length))
        {
            return nullptr;
        }

        auto* current = &g_volumes;
        cursor += length;
        while (*cursor == '/')
        {
            ++cursor;
        }

        while (*cursor != '\0')
        {
            segment = cursor;
            length = 0;
            while (cursor[length] != '\0' && cursor[length] != '/')
            {
                ++length;
            }

            current = child_by_segment(current, segment, length);
            if (current == nullptr)
            {
                return nullptr;
            }

            cursor += length;
            while (*cursor == '/')
            {
                ++cursor;
            }
        }

        return current;
    }

    bool owns(const Node* node)
    {
        for (size_t index = 0; index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++index)
        {
            if (node == g_nodes[index])
            {
                return true;
            }
        }

        return false;
    }

    size_t child_count(const Node* node)
    {
        if (!g_ready || node == nullptr || !node->directory || !owns(node))
        {
            return 0;
        }

        size_t count = 0;
        for (size_t index = 0; index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++index)
        {
            if (g_nodes[index]->parent == node)
            {
                ++count;
            }
        }

        return count;
    }

    const Node* child_at(const Node* node, size_t index)
    {
        if (!g_ready || node == nullptr || !node->directory || !owns(node))
        {
            return nullptr;
        }

        size_t current_child = 0;
        for (size_t node_index = 0; node_index < sizeof(g_nodes) / sizeof(g_nodes[0]); ++node_index)
        {
            if (g_nodes[node_index]->parent != node)
            {
                continue;
            }

            if (current_child == index)
            {
                return g_nodes[node_index];
            }

            ++current_child;
        }

        return nullptr;
    }

    bool read_file(const Node* node, const char*& data, size_t& size)
    {
        data = nullptr;
        size = 0;
        if (!g_ready || node == nullptr || node->directory || !owns(node))
        {
            return false;
        }

        if (node == &g_volume_info && !refresh_volume_text())
        {
            return false;
        }

        data = node->readonly_data;
        size = node->size;
        return data != nullptr;
    }

    bool write_file(const char* path, const char* data, size_t size)
    {
        (void)path;
        (void)data;
        (void)size;
        return false;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        const auto* volume = find("/volumes/ram-block0/volume.txt");
        const char* data = nullptr;
        size_t size = 0;
        return volume != nullptr && tinyos::kernel::vfs::blockfs::read_file(volume, data, size) && data != nullptr && size != 0;
    }

    const char* mount_path()
    {
        return MountPath;
    }

    const char* mounted_device_name()
    {
        return DeviceName;
    }
}