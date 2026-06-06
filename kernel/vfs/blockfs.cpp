#include <stdint.h>

#include <tinyos/kernel/device/block.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>

namespace
{
    constexpr char MountPath[] = "/volumes";
    constexpr char RamDeviceName[] = "ram-block0";
    constexpr char VirtioDeviceName[] = "virtio-blk0";
    constexpr char RamDeviceInfoText[] = "name=ram-block0\nclass=block\nmounted-at=/volumes/ram-block0\nmode=read-only\n";
    constexpr size_t SectorBufferSize = 512;
    constexpr size_t MaxCatalogEntries = 7;
    constexpr size_t CatalogMagicSize = 4;
    constexpr uint8_t CatalogVersion = 1;
    constexpr size_t CatalogSector0Offset = 256;
    constexpr size_t CatalogEntryBase = 64;
    constexpr size_t CatalogEntryStride = 64;
    constexpr size_t CatalogEntryFlagsWritable = 1u;
    constexpr size_t StaticNodeCount = 4;
    constexpr size_t MaxBlockNodes = StaticNodeCount + MaxCatalogEntries;
    constexpr uint32_t InvalidCatalogSizeOffset = 0xFFFFFFFFu;

    uint8_t g_sector_buffer[SectorBufferSize] = {};
    char g_volume_text[SectorBufferSize + 1] = {};
    const char* g_active_device_name = RamDeviceName;

    uint32_t g_catalog_sectors[MaxCatalogEntries] = {};
    uint32_t g_catalog_size_field_offsets[MaxCatalogEntries] = {};

    tinyos::kernel::vfs::Node g_volumes = { "volumes", true, nullptr, nullptr, 0, 0, false, nullptr };
    tinyos::kernel::vfs::Node g_primary_block = { RamDeviceName, true, nullptr, nullptr, 0, 0, false, &g_volumes };
    tinyos::kernel::vfs::Node g_device_info = { "device.txt", false, RamDeviceInfoText, nullptr, sizeof(RamDeviceInfoText) - 1, 0, false, &g_primary_block };
    tinyos::kernel::vfs::Node g_volume_info = { "volume.txt", false, g_volume_text, nullptr, 0, 0, false, &g_primary_block };

    char g_catalog_names[MaxCatalogEntries][48] = {};
    char g_catalog_data[MaxCatalogEntries][SectorBufferSize] = {};
    tinyos::kernel::vfs::Node g_catalog_nodes[MaxCatalogEntries] = {};
    tinyos::kernel::vfs::Node* g_nodes[MaxBlockNodes] = {};
    size_t g_node_count = 0;
    size_t g_catalog_count = 0;

    bool g_ready = false;

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

    void register_node(tinyos::kernel::vfs::Node* node)
    {
        if (g_node_count < MaxBlockNodes)
        {
            g_nodes[g_node_count++] = node;
        }
    }

    void rebuild_node_list()
    {
        g_node_count = 0;
        register_node(&g_volumes);
        register_node(&g_primary_block);
        register_node(&g_device_info);
        register_node(&g_volume_info);
        for (size_t index = 0; index < g_catalog_count; ++index)
        {
            register_node(&g_catalog_nodes[index]);
        }
    }

    tinyos::kernel::vfs::Node* child_by_segment(tinyos::kernel::vfs::Node* parent, const char* segment, size_t length)
    {
        if (parent == nullptr || !parent->directory)
        {
            return nullptr;
        }

        for (size_t index = 0; index < g_node_count; ++index)
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

    uint32_t read_u32_le(size_t offset)
    {
        return static_cast<uint32_t>(g_sector_buffer[offset]) |
            (static_cast<uint32_t>(g_sector_buffer[offset + 1]) << 8) |
            (static_cast<uint32_t>(g_sector_buffer[offset + 2]) << 16) |
            (static_cast<uint32_t>(g_sector_buffer[offset + 3]) << 24);
    }

    bool block_writes_enabled()
    {
        if (!tinyos::kernel::device::block::virtio_available())
        {
            return false;
        }

        const auto* device = tinyos::kernel::device::block::root_device();
        return device != nullptr && device->writable;
    }

    bool load_catalog_file(size_t catalog_index, uint32_t sector_index, uint32_t file_size, uint32_t flags, uint32_t size_field_offset);

    bool catalog_magic_matches_at(size_t base_offset)
    {
        return g_sector_buffer[base_offset + 0] == 'T' &&
            g_sector_buffer[base_offset + 1] == 'O' &&
            g_sector_buffer[base_offset + 2] == 'S' &&
            g_sector_buffer[base_offset + 3] == 'F';
    }

    bool load_catalog_from_base(size_t base_offset)
    {
        if (!catalog_magic_matches_at(base_offset))
        {
            return false;
        }

        if (g_sector_buffer[base_offset + 4] != CatalogVersion)
        {
            return false;
        }

        const uint8_t entry_count = g_sector_buffer[base_offset + 5];
        if (entry_count == 0 || entry_count > MaxCatalogEntries)
        {
            return false;
        }

        for (uint8_t entry_index = 0; entry_index < entry_count; ++entry_index)
        {
            const size_t offset = base_offset + CatalogEntryBase + static_cast<size_t>(entry_index) * CatalogEntryStride;
            for (size_t name_index = 0; name_index < 48; ++name_index)
            {
                g_catalog_names[entry_index][name_index] = static_cast<char>(g_sector_buffer[offset + name_index]);
            }

            g_catalog_names[entry_index][47] = '\0';
            if (g_catalog_names[entry_index][0] == '\0')
            {
                return false;
            }

            const uint32_t sector_index = read_u32_le(offset + 48);
            const uint32_t file_size = read_u32_le(offset + 52);
            const uint32_t flags = read_u32_le(offset + 56);
            const uint32_t size_field_offset = static_cast<uint32_t>(offset + 52);
            if (file_size > SectorBufferSize)
            {
                return false;
            }

            if (!load_catalog_file(entry_index, sector_index, file_size, flags, size_field_offset))
            {
                return false;
            }

            ++g_catalog_count;
        }

        return g_catalog_count != 0;
    }

    bool load_direct_readme_fallback()
    {
        if (!tinyos::kernel::device::block::virtio_available())
        {
            return false;
        }

        if (tinyos::kernel::device::block::read_sector(2, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        if (g_sector_buffer[0] != 'T')
        {
            return false;
        }

        g_catalog_names[0][0] = 'R';
        g_catalog_names[0][1] = 'E';
        g_catalog_names[0][2] = 'A';
        g_catalog_names[0][3] = 'D';
        g_catalog_names[0][4] = 'M';
        g_catalog_names[0][5] = 'E';
        g_catalog_names[0][6] = '.';
        g_catalog_names[0][7] = 't';
        g_catalog_names[0][8] = 'x';
        g_catalog_names[0][9] = 't';
        g_catalog_names[0][10] = '\0';

        size_t copy_size = 0;
        while (copy_size < SectorBufferSize && g_sector_buffer[copy_size] != 0)
        {
            g_catalog_data[0][copy_size] = static_cast<char>(g_sector_buffer[copy_size]);
            ++copy_size;
        }

        g_catalog_nodes[0].name = g_catalog_names[0];
        g_catalog_nodes[0].directory = false;
        g_catalog_nodes[0].readonly_data = g_catalog_data[0];
        g_catalog_nodes[0].writable_data = nullptr;
        g_catalog_nodes[0].size = copy_size;
        g_catalog_nodes[0].capacity = 0;
        g_catalog_nodes[0].writable = false;
        g_catalog_nodes[0].parent = &g_primary_block;
        g_catalog_sectors[0] = 2;
        g_catalog_size_field_offsets[0] = InvalidCatalogSizeOffset;
        g_catalog_count = 1;

        if (!block_writes_enabled())
        {
            return true;
        }

        if (tinyos::kernel::device::block::read_sector(4, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return true;
        }

        g_catalog_names[1][0] = 'n';
        g_catalog_names[1][1] = 'o';
        g_catalog_names[1][2] = 't';
        g_catalog_names[1][3] = 'e';
        g_catalog_names[1][4] = 's';
        g_catalog_names[1][5] = '.';
        g_catalog_names[1][6] = 't';
        g_catalog_names[1][7] = 'x';
        g_catalog_names[1][8] = 't';
        g_catalog_names[1][9] = '\0';

        size_t notes_size = 0;
        while (notes_size < SectorBufferSize && g_sector_buffer[notes_size] != 0)
        {
            g_catalog_data[1][notes_size] = static_cast<char>(g_sector_buffer[notes_size]);
            ++notes_size;
        }

        g_catalog_nodes[1].name = g_catalog_names[1];
        g_catalog_nodes[1].directory = false;
        g_catalog_nodes[1].readonly_data = g_catalog_data[1];
        g_catalog_nodes[1].writable_data = g_catalog_data[1];
        g_catalog_nodes[1].size = notes_size;
        g_catalog_nodes[1].capacity = SectorBufferSize;
        g_catalog_nodes[1].writable = true;
        g_catalog_nodes[1].parent = &g_primary_block;
        g_catalog_sectors[1] = 4;
        g_catalog_size_field_offsets[1] = InvalidCatalogSizeOffset;
        g_catalog_count = 2;
        return true;
    }

    bool load_catalog()
    {
        g_catalog_count = 0;
        if (tinyos::kernel::device::block::read_sector(0, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return load_direct_readme_fallback();
        }

        if (load_catalog_from_base(CatalogSector0Offset))
        {
            return true;
        }

        g_catalog_count = 0;
        if (tinyos::kernel::device::block::read_sector(1, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return load_direct_readme_fallback();
        }

        if (load_catalog_from_base(0))
        {
            return true;
        }

        return load_direct_readme_fallback();
    }

    bool load_catalog_file(size_t catalog_index, uint32_t sector_index, uint32_t file_size, uint32_t flags, uint32_t size_field_offset)
    {
        if (catalog_index >= MaxCatalogEntries || file_size > SectorBufferSize)
        {
            return false;
        }

        if (tinyos::kernel::device::block::read_sector(sector_index, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        size_t copy_size = file_size;
        if (copy_size > SectorBufferSize)
        {
            copy_size = SectorBufferSize;
        }

        for (size_t index = 0; index < SectorBufferSize; ++index)
        {
            g_catalog_data[catalog_index][index] = index < copy_size ? static_cast<char>(g_sector_buffer[index]) : '\0';
        }

        const bool writable = block_writes_enabled() && (flags & CatalogEntryFlagsWritable) != 0;
        g_catalog_nodes[catalog_index].name = g_catalog_names[catalog_index];
        g_catalog_nodes[catalog_index].directory = false;
        g_catalog_nodes[catalog_index].readonly_data = g_catalog_data[catalog_index];
        g_catalog_nodes[catalog_index].writable_data = writable ? g_catalog_data[catalog_index] : nullptr;
        g_catalog_nodes[catalog_index].size = copy_size;
        g_catalog_nodes[catalog_index].capacity = writable ? SectorBufferSize : 0;
        g_catalog_nodes[catalog_index].writable = writable;
        g_catalog_nodes[catalog_index].parent = &g_primary_block;
        g_catalog_sectors[catalog_index] = sector_index;
        g_catalog_size_field_offsets[catalog_index] = writable ? size_field_offset : InvalidCatalogSizeOffset;
        return true;
    }

    bool persist_catalog_entry_size(size_t catalog_index, uint32_t file_size)
    {
        const uint32_t size_field_offset = g_catalog_size_field_offsets[catalog_index];
        if (size_field_offset == InvalidCatalogSizeOffset)
        {
            return true;
        }

        if (tinyos::kernel::device::block::read_sector(0, g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
        {
            return false;
        }

        g_sector_buffer[size_field_offset + 0] = static_cast<uint8_t>(file_size & 0xFFu);
        g_sector_buffer[size_field_offset + 1] = static_cast<uint8_t>((file_size >> 8) & 0xFFu);
        g_sector_buffer[size_field_offset + 2] = static_cast<uint8_t>((file_size >> 16) & 0xFFu);
        g_sector_buffer[size_field_offset + 3] = static_cast<uint8_t>((file_size >> 24) & 0xFFu);

        return tinyos::kernel::device::block::write_sector(0, g_sector_buffer, sizeof(g_sector_buffer)) == tinyos::kernel::device::block::Status::Ok;
    }

    bool persist_catalog_payload(size_t catalog_index, const char* data, size_t size)
    {
        for (size_t index = 0; index < SectorBufferSize; ++index)
        {
            g_sector_buffer[index] = index < size ? static_cast<uint8_t>(data[index]) : 0;
        }

        return tinyos::kernel::device::block::write_sector(g_catalog_sectors[catalog_index], g_sector_buffer, sizeof(g_sector_buffer)) == tinyos::kernel::device::block::Status::Ok;
    }

    size_t catalog_index_for_node(const tinyos::kernel::vfs::Node* node)
    {
        for (size_t index = 0; index < g_catalog_count; ++index)
        {
            if (&g_catalog_nodes[index] == node)
            {
                return index;
            }
        }

        return MaxCatalogEntries;
    }

    void configure_active_device()
    {
        g_active_device_name = tinyos::kernel::device::block::virtio_available() ? VirtioDeviceName : RamDeviceName;
        g_primary_block.name = g_active_device_name;
        if (tinyos::kernel::device::block::virtio_available())
        {
            g_device_info.readonly_data = "name=virtio-blk0\nclass=block\nmounted-at=/volumes/virtio-blk0\nmode=read-write\n";
            g_device_info.size = 78;
        }
        else
        {
            g_device_info.readonly_data = RamDeviceInfoText;
            g_device_info.size = sizeof(RamDeviceInfoText) - 1;
        }
    }
}

namespace tinyos::kernel::vfs::blockfs
{
    void initialize()
    {
        configure_active_device();
        rebuild_node_list();
        const bool volume_ready = tinyos::kernel::device::block::is_ready() && refresh_volume_text();
        const bool catalog_ready = volume_ready && load_catalog();
        g_ready = volume_ready;
        if (g_ready)
        {
            rebuild_node_list();
            if (catalog_ready)
            {
                kernel::klog::write_line(kernel::klog::Level::Info, "Block catalog loaded.");
            }

            if (block_writes_enabled())
            {
                for (size_t index = 0; index < g_catalog_count; ++index)
                {
                    if (g_catalog_nodes[index].writable)
                    {
                        kernel::klog::write_line(kernel::klog::Level::Info, "Block writable store ready.");
                        break;
                    }
                }
            }

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
        for (size_t index = 0; index < g_node_count; ++index)
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
        for (size_t index = 0; index < g_node_count; ++index)
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
        for (size_t node_index = 0; node_index < g_node_count; ++node_index)
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

        const size_t catalog_index = catalog_index_for_node(node);
        if (catalog_index < g_catalog_count && g_catalog_nodes[catalog_index].writable)
        {
            if (tinyos::kernel::device::block::read_sector(g_catalog_sectors[catalog_index], g_sector_buffer, sizeof(g_sector_buffer)) != tinyos::kernel::device::block::Status::Ok)
            {
                return false;
            }

            size_t copy_size = 0;
            while (copy_size < SectorBufferSize && g_sector_buffer[copy_size] != 0)
            {
                ++copy_size;
            }

            for (size_t index = 0; index < SectorBufferSize; ++index)
            {
                g_catalog_data[catalog_index][index] = index < copy_size ? static_cast<char>(g_sector_buffer[index]) : '\0';
            }

            g_catalog_nodes[catalog_index].size = copy_size;
            g_catalog_nodes[catalog_index].readonly_data = g_catalog_data[catalog_index];
            data = g_catalog_nodes[catalog_index].readonly_data;
            size = copy_size;
            return data != nullptr;
        }

        data = node->readonly_data;
        size = node->size;
        return data != nullptr;
    }

    bool write_file(const char* path, const char* data, size_t size)
    {
        if (!g_ready || path == nullptr || data == nullptr || !block_writes_enabled())
        {
            return false;
        }

        auto* node = const_cast<Node*>(find(path));
        if (node == nullptr || node->directory || !node->writable || node->writable_data == nullptr || size > SectorBufferSize)
        {
            return false;
        }

        const size_t catalog_index = catalog_index_for_node(node);
        if (catalog_index >= g_catalog_count)
        {
            return false;
        }

        for (size_t index = 0; index < size; ++index)
        {
            node->writable_data[index] = data[index];
        }

        for (size_t index = size; index < SectorBufferSize; ++index)
        {
            node->writable_data[index] = '\0';
        }

        node->size = size;
        node->readonly_data = node->writable_data;

        if (!persist_catalog_payload(catalog_index, data, size))
        {
            return false;
        }

        return persist_catalog_entry_size(catalog_index, static_cast<uint32_t>(size));
    }

    bool writable_store_self_test()
    {
        if (!g_ready || !block_writes_enabled() || g_catalog_count == 0)
        {
            return true;
        }

        char path[64] = {};
        size_t path_length = 0;
        const char* prefix = "/volumes/";
        while (prefix[path_length] != '\0' && path_length + 1 < sizeof(path))
        {
            path[path_length] = prefix[path_length];
            ++path_length;
        }

        const char* name = mounted_device_name();
        for (size_t index = 0; name[index] != '\0' && path_length + 16 < sizeof(path); ++index)
        {
            path[path_length++] = name[index];
        }

        const char* notes_suffix = "/notes.txt";
        for (size_t index = 0; notes_suffix[index] != '\0' && path_length + 1 < sizeof(path); ++index)
        {
            path[path_length++] = notes_suffix[index];
        }

        path[path_length] = '\0';
        const auto* notes = find(path);
        if (notes == nullptr || !notes->writable)
        {
            return true;
        }

        constexpr char TestPayload[] = "TinyOS block write test.\n";
        if (!tinyos::kernel::vfs::blockfs::write_file(path, TestPayload, sizeof(TestPayload) - 1))
        {
            return false;
        }

        const char* verify_data = nullptr;
        size_t verify_size = 0;
        if (!tinyos::kernel::vfs::blockfs::read_file(notes, verify_data, verify_size))
        {
            return false;
        }

        if (verify_data == nullptr || verify_size != sizeof(TestPayload) - 1)
        {
            return false;
        }

        for (size_t index = 0; index < verify_size; ++index)
        {
            if (verify_data[index] != TestPayload[index])
            {
                return false;
            }
        }

        return true;
    }

    bool validation_self_test()
    {
        if (!g_ready)
        {
            return false;
        }

        char path[64] = {};
        size_t path_length = 0;
        const char* prefix = "/volumes/";
        while (prefix[path_length] != '\0' && path_length + 1 < sizeof(path))
        {
            path[path_length] = prefix[path_length];
            ++path_length;
        }

        const char* name = mounted_device_name();
        for (size_t index = 0; name[index] != '\0' && path_length + 6 < sizeof(path); ++index)
        {
            path[path_length++] = name[index];
        }

        const char* suffix = "/volume.txt";
        for (size_t index = 0; suffix[index] != '\0' && path_length + 1 < sizeof(path); ++index)
        {
            path[path_length++] = suffix[index];
        }

        path[path_length] = '\0';

        const auto* volume = find(path);
        const char* data = nullptr;
        size_t data_size = 0;
        if (volume == nullptr || !tinyos::kernel::vfs::blockfs::read_file(volume, data, data_size) || data == nullptr || data_size == 0)
        {
            return false;
        }

        if (g_catalog_count == 0)
        {
            return writable_store_self_test();
        }

        path_length = 0;
        while (prefix[path_length] != '\0' && path_length + 1 < sizeof(path))
        {
            path[path_length] = prefix[path_length];
            ++path_length;
        }

        name = mounted_device_name();
        for (size_t index = 0; name[index] != '\0' && path_length + 16 < sizeof(path); ++index)
        {
            path[path_length++] = name[index];
        }

        const char* readme_suffix = "/README.txt";
        for (size_t index = 0; readme_suffix[index] != '\0' && path_length + 1 < sizeof(path); ++index)
        {
            path[path_length++] = readme_suffix[index];
        }

        path[path_length] = '\0';
        const auto* readme = find(path);
        data = nullptr;
        data_size = 0;
        return readme != nullptr &&
            tinyos::kernel::vfs::blockfs::read_file(readme, data, data_size) &&
            data != nullptr &&
            data_size != 0 &&
            data[0] == 'T' &&
            writable_store_self_test();
    }

    const char* mount_path()
    {
        return MountPath;
    }

    const char* mounted_device_name()
    {
        return g_active_device_name;
    }

    const char* primary_volume_path()
    {
        return tinyos::kernel::device::block::virtio_available()
            ? "/volumes/virtio-blk0/volume.txt"
            : "/volumes/ram-block0/volume.txt";
    }
}
