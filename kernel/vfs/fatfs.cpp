#include <tinyos/drivers/ata.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/fatfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>

namespace
{
    constexpr char MountPath[] = "/mnt/fat";
    constexpr uint16_t BytesPerSector = 512;
    constexpr uint8_t SectorsPerCluster = 1;
    constexpr uint16_t ReservedSectors = 1;
    constexpr uint8_t FatCount = 2;
    constexpr uint16_t RootEntryCount = 64;
    constexpr uint8_t Fat16Media = 0xF8;
    constexpr size_t MaxVfsFiles = 8;
    constexpr size_t MaxFileBytes = 480;
    constexpr size_t MaxRootEntries = 128;

    struct [[gnu::packed]] BiosParameterBlock
    {
        uint8_t jump[3];
        char oem[8];
        uint16_t bytes_per_sector;
        uint8_t sectors_per_cluster;
        uint16_t reserved_sector_count;
        uint8_t fat_count;
        uint16_t root_entry_count;
        uint16_t total_sectors16;
        uint8_t media;
        uint16_t fat_size16;
        uint16_t sectors_per_track;
        uint16_t head_count;
        uint32_t hidden_sectors;
        uint32_t total_sectors32;
        uint8_t drive_number;
        uint8_t reserved1;
        uint8_t boot_signature;
        uint32_t volume_id;
        char volume_label[11];
        char fs_type[8];
    };

    struct [[gnu::packed]] DirEntry
    {
        char name[11];
        uint8_t attributes;
        uint8_t nt_reserved;
        uint8_t create_tenth;
        uint16_t create_time;
        uint16_t create_date;
        uint16_t access_date;
        uint16_t first_cluster_high;
        uint16_t modify_time;
        uint16_t modify_date;
        uint16_t first_cluster_low;
        uint32_t size;
    };

    uint8_t g_sector[BytesPerSector] = {};
    char g_file_storage[MaxVfsFiles][MaxFileBytes + 1] = {};
    char g_file_names[MaxVfsFiles][13] = {};
    tinyos::kernel::vfs::Node g_file_nodes[MaxVfsFiles] = {};
    tinyos::kernel::vfs::Node g_mnt = { "mnt", true, nullptr, nullptr, 0, 0, false, nullptr };
    tinyos::kernel::vfs::Node g_fat = { "fat", true, nullptr, nullptr, 0, 0, false, &g_mnt };
    tinyos::kernel::vfs::Node g_info = { "fsinfo.txt", false, nullptr, nullptr, 0, 0, false, &g_fat };
    char g_info_text[160] = {};
    size_t g_file_count = 0;
    uint16_t g_fat_sectors = 1;
    uint16_t g_root_sectors = 1;
    uint16_t g_reserved_sectors = ReservedSectors;
    uint16_t g_root_entry_count = RootEntryCount;
    uint8_t g_fat_count = FatCount;
    uint8_t g_sectors_per_cluster = SectorsPerCluster;
    uint32_t g_data_start = 0;
    bool g_ready = false;

    void clear_sector()
    {
        for (size_t index = 0; index < BytesPerSector; ++index)
        {
            g_sector[index] = 0;
        }
    }

    bool backend_ready()
    {
        const auto* device = tinyos::drivers::ata::device();
        return tinyos::drivers::ata::is_ready() &&
            device != nullptr &&
            device->sector_count >= 64 &&
            device->sector_size == BytesPerSector;
    }

    bool read_sector(uint32_t index)
    {
        return tinyos::drivers::ata::read_sector(index, g_sector, BytesPerSector) ==
            tinyos::kernel::device::block::Status::Ok;
    }

    bool write_sector(uint32_t index)
    {
        return tinyos::drivers::ata::write_sector(index, g_sector, BytesPerSector) ==
            tinyos::kernel::device::block::Status::Ok;
    }

    uint32_t total_sectors()
    {
        const auto* device = tinyos::drivers::ata::device();
        return device != nullptr ? device->sector_count : 0;
    }

    void fill_bpb(BiosParameterBlock& bpb, uint32_t sectors, uint16_t fat_sectors)
    {
        bpb.jump[0] = 0xEB;
        bpb.jump[1] = 0x3C;
        bpb.jump[2] = 0x90;
        const char oem[] = "TINYOS16";
        for (size_t index = 0; index < 8; ++index)
        {
            bpb.oem[index] = oem[index];
        }

        bpb.bytes_per_sector = BytesPerSector;
        bpb.sectors_per_cluster = SectorsPerCluster;
        bpb.reserved_sector_count = ReservedSectors;
        bpb.fat_count = FatCount;
        bpb.root_entry_count = RootEntryCount;
        bpb.total_sectors16 = sectors <= 0xFFFF ? static_cast<uint16_t>(sectors) : 0;
        bpb.media = Fat16Media;
        bpb.fat_size16 = fat_sectors;
        bpb.sectors_per_track = 32;
        bpb.head_count = 16;
        bpb.hidden_sectors = 0;
        bpb.total_sectors32 = sectors > 0xFFFF ? sectors : 0;
        bpb.drive_number = 0x80;
        bpb.reserved1 = 0;
        bpb.boot_signature = 0x29;
        bpb.volume_id = 0x544F5331;
        const char label[] = "TINYOSFAT  ";
        const char type[] = "FAT16   ";
        for (size_t index = 0; index < 11; ++index)
        {
            bpb.volume_label[index] = label[index];
        }
        for (size_t index = 0; index < 8; ++index)
        {
            bpb.fs_type[index] = type[index];
        }
    }

    bool has_fat_signature()
    {
        if (!read_sector(0))
        {
            return false;
        }

        return g_sector[510] == 0x55 && g_sector[511] == 0xAA &&
            g_sector[0x36] == 'F' && g_sector[0x37] == 'A' && g_sector[0x38] == 'T';
    }

    uint16_t compute_fat_sectors(uint32_t sectors)
    {
        const uint32_t bytes = sectors * 2u + 4u;
        uint16_t count = static_cast<uint16_t>((bytes + BytesPerSector - 1) / BytesPerSector);
        return count == 0 ? 1 : count;
    }

    void to_83_name(const char* name, char out[11])
    {
        for (size_t index = 0; index < 11; ++index)
        {
            out[index] = ' ';
        }

        size_t out_index = 0;
        for (size_t index = 0; name[index] != '\0' && out_index < 11; ++index)
        {
            char ch = name[index];
            if (ch == '.')
            {
                out_index = 8;
                continue;
            }

            if (ch >= 'a' && ch <= 'z')
            {
                ch = static_cast<char>(ch - 'a' + 'A');
            }

            if (out_index < 11)
            {
                out[out_index++] = ch;
            }
        }
    }

    void from_83_name(const char name[11], char out[13])
    {
        size_t out_index = 0;
        for (size_t index = 0; index < 8 && name[index] != ' '; ++index)
        {
            out[out_index++] = name[index];
        }

        if (name[8] != ' ')
        {
            out[out_index++] = '.';
            for (size_t index = 8; index < 11 && name[index] != ' '; ++index)
            {
                out[out_index++] = name[index];
            }
        }

        out[out_index] = '\0';
    }

    bool format_volume()
    {
        const uint32_t sectors = total_sectors();
        g_reserved_sectors = ReservedSectors;
        g_root_entry_count = RootEntryCount;
        g_fat_count = FatCount;
        g_sectors_per_cluster = SectorsPerCluster;
        g_fat_sectors = compute_fat_sectors(sectors);
        g_root_sectors = static_cast<uint16_t>((g_root_entry_count * 32u + BytesPerSector - 1) / BytesPerSector);
        g_data_start = g_reserved_sectors + (g_fat_count * g_fat_sectors) + g_root_sectors;

        clear_sector();
        fill_bpb(*reinterpret_cast<BiosParameterBlock*>(g_sector), sectors, g_fat_sectors);
        g_sector[510] = 0x55;
        g_sector[511] = 0xAA;
        if (!write_sector(0))
        {
            return false;
        }

        for (uint8_t fat_index = 0; fat_index < g_fat_count; ++fat_index)
        {
            for (uint16_t sector = 0; sector < g_fat_sectors; ++sector)
            {
                clear_sector();
                if (sector == 0)
                {
                    g_sector[0] = Fat16Media;
                    g_sector[1] = 0xFF;
                    g_sector[2] = 0xFF;
                    g_sector[3] = 0xFF;
                }

                if (!write_sector(g_reserved_sectors + fat_index * g_fat_sectors + sector))
                {
                    return false;
                }
            }
        }

        for (uint16_t sector = 0; sector < g_root_sectors; ++sector)
        {
            clear_sector();
            if (!write_sector(g_reserved_sectors + g_fat_count * g_fat_sectors + sector))
            {
                return false;
            }
        }

        return true;
    }

    bool load_geometry_from_bpb()
    {
        if (!read_sector(0))
        {
            return false;
        }

        const auto* bpb = reinterpret_cast<const BiosParameterBlock*>(g_sector);
        if (bpb->bytes_per_sector != BytesPerSector ||
            bpb->fat_count == 0 ||
            bpb->fat_size16 == 0 ||
            bpb->sectors_per_cluster == 0 ||
            bpb->root_entry_count == 0 ||
            bpb->root_entry_count > MaxRootEntries)
        {
            return false;
        }

        g_reserved_sectors = bpb->reserved_sector_count == 0 ? ReservedSectors : bpb->reserved_sector_count;
        g_root_entry_count = bpb->root_entry_count;
        g_fat_count = bpb->fat_count;
        g_sectors_per_cluster = bpb->sectors_per_cluster;
        g_fat_sectors = bpb->fat_size16;
        g_root_sectors = static_cast<uint16_t>((g_root_entry_count * 32u + BytesPerSector - 1) / BytesPerSector);
        g_data_start = g_reserved_sectors + (g_fat_count * g_fat_sectors) + g_root_sectors;
        return true;
    }

    uint32_t cluster_to_sector(uint16_t cluster)
    {
        return g_data_start + static_cast<uint32_t>(cluster - 2) * g_sectors_per_cluster;
    }

    uint32_t root_directory_sector()
    {
        return g_reserved_sectors + g_fat_count * g_fat_sectors;
    }

    bool names_equal_83(const char left[11], const char right[11])
    {
        for (size_t index = 0; index < 11; ++index)
        {
            if (left[index] != right[index])
            {
                return false;
            }
        }

        return true;
    }

    bool allocate_cluster(uint16_t& cluster)
    {
        for (uint16_t candidate = 2; candidate < 512; ++candidate)
        {
            const uint32_t fat_offset = static_cast<uint32_t>(candidate) * 2u;
            const uint32_t fat_sector = g_reserved_sectors + (fat_offset / BytesPerSector);
            const uint32_t fat_index = fat_offset % BytesPerSector;
            if (!read_sector(fat_sector))
            {
                return false;
            }

            const uint16_t value = static_cast<uint16_t>(g_sector[fat_index] | (g_sector[fat_index + 1] << 8));
            if (value != 0)
            {
                continue;
            }

            g_sector[fat_index] = 0xFF;
            g_sector[fat_index + 1] = 0xFF;
            if (!write_sector(fat_sector))
            {
                return false;
            }

            if (g_fat_count > 1 && !write_sector(fat_sector + g_fat_sectors))
            {
                return false;
            }

            cluster = candidate;
            return true;
        }

        return false;
    }

    bool path_prefix_match(const char* path, const char* prefix)
    {
        size_t index = 0;
        while (prefix[index] != '\0')
        {
            if (path[index] != prefix[index])
            {
                return false;
            }
            ++index;
        }

        return path[index] == '\0' || path[index] == '/';
    }

    bool sync_files()
    {
        g_file_count = 0;
        const uint32_t root_sector = root_directory_sector();
        if (!read_sector(root_sector))
        {
            return false;
        }

        auto* entries = reinterpret_cast<DirEntry*>(g_sector);
        for (size_t index = 0; index < g_root_entry_count && g_file_count < MaxVfsFiles; ++index)
        {
            // Root may span multiple sectors; keep the simple one-sector path for now.
            if (index >= (BytesPerSector / sizeof(DirEntry)))
            {
                break;
            }

            if (entries[index].name[0] == 0x00 || static_cast<uint8_t>(entries[index].name[0]) == 0xE5)
            {
                continue;
            }

            if ((entries[index].attributes & 0x18) != 0)
            {
                continue;
            }

            from_83_name(entries[index].name, g_file_names[g_file_count]);
            size_t copy = entries[index].size;
            if (copy > MaxFileBytes)
            {
                copy = MaxFileBytes;
            }

            for (size_t byte_index = 0; byte_index <= MaxFileBytes; ++byte_index)
            {
                g_file_storage[g_file_count][byte_index] = 0;
            }

            if (entries[index].first_cluster_low >= 2 && copy > 0)
            {
                uint8_t data_sector[BytesPerSector] = {};
                if (tinyos::drivers::ata::read_sector(
                        cluster_to_sector(entries[index].first_cluster_low),
                        data_sector,
                        BytesPerSector) != tinyos::kernel::device::block::Status::Ok)
                {
                    return false;
                }

                for (size_t byte_index = 0; byte_index < copy; ++byte_index)
                {
                    g_file_storage[g_file_count][byte_index] = static_cast<char>(data_sector[byte_index]);
                }
            }

            g_file_nodes[g_file_count] = {
                g_file_names[g_file_count],
                false,
                g_file_storage[g_file_count],
                g_file_storage[g_file_count],
                copy,
                MaxFileBytes,
                true,
                &g_fat
            };
            ++g_file_count;
        }

        for (size_t index = 0; index < sizeof(g_info_text); ++index)
        {
            g_info_text[index] = 0;
        }

        const char text[] = "fs=FAT16\nmount=/mnt/fat\nbackend=ata0-master\n";
        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            g_info_text[index] = text[index];
        }

        g_info.readonly_data = g_info_text;
        g_info.writable_data = nullptr;
        g_info.size = sizeof(text) - 1;
        g_info.parent = &g_fat;
        g_fat.parent = &g_mnt;
        return true;
    }
}

namespace tinyos::kernel::vfs::fatfs
{
    void initialize()
    {
        g_ready = false;
        g_file_count = 0;
        if (!backend_ready())
        {
            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Info,
                "FAT16 skipped (ATA primary master not ready).");
            return;
        }

        if (!has_fat_signature())
        {
            // Never auto-format a bootable IDE image (ISO/GRUB). Only initialize a
            // blank data disk whose first sector is still zeroed.
            if (!read_sector(0))
            {
                return;
            }

            bool blank = true;
            for (size_t index = 0; index < BytesPerSector; ++index)
            {
                if (g_sector[index] != 0)
                {
                    blank = false;
                    break;
                }
            }

            if (!blank)
            {
                tinyos::kernel::klog::write_line(
                    tinyos::kernel::klog::Level::Info,
                    "FAT16 skipped (ATA disk is not blank and has no FAT signature).");
                return;
            }

            if (!format_volume())
            {
                tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "FAT16 format failed.");
                return;
            }

            tinyos::kernel::klog::write_line(
                tinyos::kernel::klog::Level::Info,
                "FAT16 volume formatted on blank ATA disk.");
        }
        else if (!load_geometry_from_bpb())
        {
            tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, "FAT16 BPB parse failed.");
            return;
        }

        if (!sync_files())
        {
            return;
        }

        g_ready = true;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "FAT16 mounted at /mnt/fat.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool format_if_needed()
    {
        if (!backend_ready())
        {
            return false;
        }

        if (has_fat_signature())
        {
            return load_geometry_from_bpb();
        }

        return format_volume();
    }

    bool mount()
    {
        if (!g_ready)
        {
            initialize();
        }

        return g_ready;
    }

    const char* mount_path()
    {
        return MountPath;
    }

    const tinyos::kernel::vfs::Node* root()
    {
        return g_ready ? &g_mnt : nullptr;
    }

    bool owns(const tinyos::kernel::vfs::Node* node)
    {
        if (!g_ready || node == nullptr)
        {
            return false;
        }

        if (node == &g_mnt || node == &g_fat || node == &g_info)
        {
            return true;
        }

        for (size_t index = 0; index < g_file_count; ++index)
        {
            if (node == &g_file_nodes[index])
            {
                return true;
            }
        }

        return false;
    }

    const tinyos::kernel::vfs::Node* find(const char* path)
    {
        if (!g_ready || path == nullptr)
        {
            return nullptr;
        }

        if (path[0] == '/' && path[1] == 'm' && path[2] == 'n' && path[3] == 't' && path[4] == '\0')
        {
            return &g_mnt;
        }

        if (!path_prefix_match(path, MountPath))
        {
            return nullptr;
        }

        if (path[0] == '/' && path[1] == 'm' && path[2] == 'n' && path[3] == 't' &&
            path[4] == '/' && path[5] == 'f' && path[6] == 'a' && path[7] == 't')
        {
            if (path[8] == '\0')
            {
                return &g_fat;
            }

            if (path[8] != '/')
            {
                return nullptr;
            }

            const char* leaf = path + 9;
            if (leaf[0] == 'f' && leaf[1] == 's' && leaf[2] == 'i' && leaf[3] == 'n' &&
                leaf[4] == 'f' && leaf[5] == 'o' && leaf[6] == '.' && leaf[7] == 't' &&
                leaf[8] == 'x' && leaf[9] == 't' && leaf[10] == '\0')
            {
                return &g_info;
            }

            for (size_t index = 0; index < g_file_count; ++index)
            {
                size_t n = 0;
                while (g_file_names[index][n] != '\0' && leaf[n] != '\0' && g_file_names[index][n] == leaf[n])
                {
                    ++n;
                }

                if (g_file_names[index][n] == '\0' && leaf[n] == '\0')
                {
                    return &g_file_nodes[index];
                }
            }
        }

        return nullptr;
    }

    size_t child_count(const tinyos::kernel::vfs::Node* node)
    {
        if (!owns(node) || node == nullptr || !node->directory)
        {
            return 0;
        }

        if (node == &g_mnt)
        {
            return 1;
        }

        if (node == &g_fat)
        {
            return 1 + g_file_count;
        }

        return 0;
    }

    const tinyos::kernel::vfs::Node* child_at(const tinyos::kernel::vfs::Node* node, size_t index)
    {
        if (!owns(node) || node == nullptr || !node->directory)
        {
            return nullptr;
        }

        if (node == &g_mnt)
        {
            return index == 0 ? &g_fat : nullptr;
        }

        if (node == &g_fat)
        {
            if (index == 0)
            {
                return &g_info;
            }

            if (index - 1 < g_file_count)
            {
                return &g_file_nodes[index - 1];
            }
        }

        return nullptr;
    }

    bool write_file(const char* name, const char* data, size_t size)
    {
        if (!g_ready || name == nullptr || data == nullptr || size > MaxFileBytes)
        {
            return false;
        }

        char want[11];
        to_83_name(name, want);

        const uint32_t root_sector = root_directory_sector();
        if (!read_sector(root_sector))
        {
            return false;
        }

        auto* entries = reinterpret_cast<DirEntry*>(g_sector);
        const size_t entries_per_sector = BytesPerSector / sizeof(DirEntry);
        const size_t scan_count = g_root_entry_count < entries_per_sector ? g_root_entry_count : entries_per_sector;

        size_t slot = scan_count;
        uint16_t cluster = 0;
        bool reuse_cluster = false;
        for (size_t index = 0; index < scan_count; ++index)
        {
            if (entries[index].name[0] != 0x00 &&
                static_cast<uint8_t>(entries[index].name[0]) != 0xE5 &&
                names_equal_83(entries[index].name, want))
            {
                slot = index;
                if (entries[index].first_cluster_low >= 2)
                {
                    cluster = entries[index].first_cluster_low;
                    reuse_cluster = true;
                }
                break;
            }

            if (slot == scan_count &&
                (entries[index].name[0] == 0x00 || static_cast<uint8_t>(entries[index].name[0]) == 0xE5))
            {
                slot = index;
            }
        }

        if (slot >= scan_count)
        {
            return false;
        }

        if (!reuse_cluster && !allocate_cluster(cluster))
        {
            return false;
        }

        // Re-read root: allocate_cluster overwrites g_sector.
        if (!read_sector(root_sector))
        {
            return false;
        }

        entries = reinterpret_cast<DirEntry*>(g_sector);

        clear_sector();
        for (size_t index = 0; index < size; ++index)
        {
            g_sector[index] = static_cast<uint8_t>(data[index]);
        }

        if (!write_sector(cluster_to_sector(cluster)))
        {
            return false;
        }

        if (!read_sector(root_sector))
        {
            return false;
        }

        entries = reinterpret_cast<DirEntry*>(g_sector);
        for (size_t index = 0; index < sizeof(DirEntry); ++index)
        {
            reinterpret_cast<uint8_t*>(&entries[slot])[index] = 0;
        }

        to_83_name(name, entries[slot].name);
        entries[slot].attributes = 0x20;
        entries[slot].first_cluster_low = cluster;
        entries[slot].size = static_cast<uint32_t>(size);
        if (!write_sector(root_sector))
        {
            return false;
        }

        return sync_files();
    }

    bool read_file(const char* name, char* buffer, size_t capacity, size_t& out_size)
    {
        out_size = 0;
        if (!g_ready || name == nullptr || buffer == nullptr || capacity == 0)
        {
            return false;
        }

        char want[11];
        to_83_name(name, want);
        const uint32_t root_sector = root_directory_sector();
        if (!read_sector(root_sector))
        {
            return false;
        }

        auto* entries = reinterpret_cast<DirEntry*>(g_sector);
        const size_t entries_per_sector = BytesPerSector / sizeof(DirEntry);
        const size_t scan_count = g_root_entry_count < entries_per_sector ? g_root_entry_count : entries_per_sector;
        for (size_t index = 0; index < scan_count; ++index)
        {
            if (!names_equal_83(entries[index].name, want))
            {
                continue;
            }

            if (!read_sector(cluster_to_sector(entries[index].first_cluster_low)))
            {
                return false;
            }

            size_t copy = entries[index].size;
            if (copy >= capacity)
            {
                copy = capacity - 1;
            }

            for (size_t byte_index = 0; byte_index < copy; ++byte_index)
            {
                buffer[byte_index] = static_cast<char>(g_sector[byte_index]);
            }

            buffer[copy] = '\0';
            out_size = copy;
            return true;
        }

        return false;
    }

    bool validation_self_test()
    {
        if (!backend_ready())
        {
            return true;
        }

        if (!g_ready)
        {
            return false;
        }

        if (find("/mnt/fat/fsinfo.txt") == nullptr)
        {
            return false;
        }

        const char payload[] = "tinyos-fat16-ok";
        if (!write_file("PROBE.TXT", payload, sizeof(payload) - 1))
        {
            return false;
        }

        char buffer[64] = {};
        size_t size = 0;
        if (!read_file("PROBE.TXT", buffer, sizeof(buffer), size) || size != sizeof(payload) - 1)
        {
            return false;
        }

        for (size_t index = 0; index < size; ++index)
        {
            if (buffer[index] != payload[index])
            {
                return false;
            }
        }

        return true;
    }
}
