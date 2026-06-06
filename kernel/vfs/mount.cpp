#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>
#include <tinyos/kernel/vfs/mount.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>

namespace
{
    constexpr size_t MaxMounts = 4;
    constexpr size_t MaxMountPathBytes = 64;

    struct Entry
    {
        char source[MaxMountPathBytes];
        char target[MaxMountPathBytes];
        bool active;
    };

    Entry g_mounts[MaxMounts];
    bool g_ready = false;

    size_t string_length(const char* text)
    {
        size_t length = 0;
        if (text == nullptr)
        {
            return 0;
        }

        while (text[length] != '\0')
        {
            ++length;
        }

        return length;
    }

    bool copy_string(char* destination, size_t capacity, const char* source)
    {
        if (destination == nullptr || source == nullptr || capacity == 0)
        {
            return false;
        }

        size_t index = 0;
        while (source[index] != '\0')
        {
            if (index + 1 >= capacity)
            {
                destination[0] = '\0';
                return false;
            }

            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
        return true;
    }

    bool strings_equal(const char* left, const char* right)
    {
        size_t index = 0;
        while (left[index] != '\0' && right[index] != '\0')
        {
            if (left[index] != right[index])
            {
                return false;
            }

            ++index;
        }

        return left[index] == '\0' && right[index] == '\0';
    }

    bool path_has_prefix(const char* path, const char* prefix)
    {
        const size_t prefix_length = string_length(prefix);
        if (prefix_length == 0)
        {
            return false;
        }

        for (size_t index = 0; index < prefix_length; ++index)
        {
            if (path[index] != prefix[index])
            {
                return false;
            }
        }

        return path[prefix_length] == '\0' || path[prefix_length] == '/';
    }

    bool append_suffix(char* destination, size_t capacity, const char* suffix)
    {
        size_t length = string_length(destination);
        if (suffix == nullptr || suffix[0] == '\0')
        {
            return length < capacity;
        }

        if (length + 1 >= capacity)
        {
            return false;
        }

        if (length > 0 && destination[length - 1] != '/' && suffix[0] != '/')
        {
            if (length + 1 >= capacity)
            {
                return false;
            }

            destination[length++] = '/';
            destination[length] = '\0';
        }

        size_t suffix_index = 0;
        if (suffix[0] == '/' && length > 0 && destination[length - 1] == '/')
        {
            suffix_index = 1;
        }

        while (suffix[suffix_index] != '\0')
        {
            if (length + 1 >= capacity)
            {
                destination[0] = '\0';
                return false;
            }

            destination[length++] = suffix[suffix_index++];
        }

        destination[length] = '\0';
        return true;
    }

    bool normalize_source_path(const char* source, char* normalized, size_t capacity)
    {
        if (!tinyos::kernel::vfs::validate_path(source))
        {
            return false;
        }

        constexpr char Disk0Alias[] = "/volumes/disk0";
        const size_t alias_length = string_length(Disk0Alias);
        if (path_has_prefix(source, Disk0Alias) &&
            (source[alias_length] == '\0' || source[alias_length] == '/'))
        {
            char rebuilt[MaxMountPathBytes];
            if (!copy_string(rebuilt, sizeof(rebuilt), "/volumes/"))
            {
                return false;
            }

            const char* device_name = tinyos::kernel::vfs::blockfs::mounted_device_name();
            if (device_name == nullptr || device_name[0] == '\0')
            {
                return false;
            }

            if (!append_suffix(rebuilt, sizeof(rebuilt), device_name))
            {
                return false;
            }

            if (source[alias_length] == '/')
            {
                if (!append_suffix(rebuilt, sizeof(rebuilt), source + alias_length))
                {
                    return false;
                }
            }

            return copy_string(normalized, capacity, rebuilt);
        }

        return copy_string(normalized, capacity, source);
    }

    bool is_block_volume_directory(const char* path)
    {
        if (!tinyos::kernel::vfs::blockfs::is_ready() || path == nullptr)
        {
            return false;
        }

        char device_path[MaxMountPathBytes];
        device_path[0] = '\0';
        if (copy_string(device_path, sizeof(device_path), "/volumes/"))
        {
            const char* device_name = tinyos::kernel::vfs::blockfs::mounted_device_name();
            if (device_name != nullptr && device_name[0] != '\0' &&
                append_suffix(device_path, sizeof(device_path), device_name) &&
                strings_equal(device_path, path))
            {
                return true;
            }
        }

        const auto* node = tinyos::kernel::vfs::blockfs::find(path);
        return node != nullptr && node->directory && path_has_prefix(path, "/volumes/");
    }

    bool targets_overlap(const char* left, const char* right)
    {
        return path_has_prefix(left, right) || path_has_prefix(right, left);
    }

    bool target_available(const char* target)
    {
        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (!g_mounts[index].active)
            {
                continue;
            }

            if (strings_equal(g_mounts[index].target, target) || targets_overlap(g_mounts[index].target, target))
            {
                return false;
            }
        }

        return true;
    }

    Entry* find_mount_by_target(const char* target)
    {
        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (g_mounts[index].active && strings_equal(g_mounts[index].target, target))
            {
                return &g_mounts[index];
            }
        }

        return nullptr;
    }
}

namespace tinyos::kernel::vfs::mount
{
    void initialize()
    {
        for (size_t index = 0; index < MaxMounts; ++index)
        {
            g_mounts[index].source[0] = '\0';
            g_mounts[index].target[0] = '\0';
            g_mounts[index].active = false;
        }

        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "VFS mount registry ready.");
    }

    bool mount_known_block_directory(const char* source, const char* target)
    {
        if (!g_ready || source == nullptr || target == nullptr)
        {
            return false;
        }

        if (!tinyos::kernel::vfs::validate_path(target) || !tinyos::kernel::vfs::block_mount_ready())
        {
            return false;
        }

        char normalized_source[MaxMountPathBytes];
        normalized_source[0] = '\0';
        if (!normalize_source_path(source, normalized_source, sizeof(normalized_source)))
        {
            return false;
        }

        if (path_has_prefix(normalized_source, target) || path_has_prefix(target, "/volumes"))
        {
            return false;
        }

        if (!target_available(target))
        {
            return false;
        }

        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (g_mounts[index].active)
            {
                continue;
            }

            if (!copy_string(g_mounts[index].source, sizeof(g_mounts[index].source), normalized_source))
            {
                return false;
            }

            if (!copy_string(g_mounts[index].target, sizeof(g_mounts[index].target), target))
            {
                g_mounts[index].source[0] = '\0';
                return false;
            }

            g_mounts[index].active = true;
            return true;
        }

        return false;
    }

    bool mount(const char* source, const char* target)
    {
        if (!g_ready || source == nullptr || target == nullptr)
        {
            return false;
        }

        if (!tinyos::kernel::vfs::validate_path(target) || !tinyos::kernel::vfs::block_mount_ready())
        {
            return false;
        }

        char normalized_source[MaxMountPathBytes];
        normalized_source[0] = '\0';
        if (!normalize_source_path(source, normalized_source, sizeof(normalized_source)))
        {
            return false;
        }

        if (!is_block_volume_directory(normalized_source))
        {
            return false;
        }

        if (path_has_prefix(normalized_source, target) || path_has_prefix(target, "/volumes"))
        {
            return false;
        }

        if (!target_available(target))
        {
            return false;
        }

        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (g_mounts[index].active)
            {
                continue;
            }

            if (!copy_string(g_mounts[index].source, sizeof(g_mounts[index].source), normalized_source))
            {
                return false;
            }

            if (!copy_string(g_mounts[index].target, sizeof(g_mounts[index].target), target))
            {
                g_mounts[index].source[0] = '\0';
                return false;
            }

            g_mounts[index].active = true;
            return true;
        }

        return false;
    }

    bool unmount(const char* target)
    {
        if (!g_ready || target == nullptr || !tinyos::kernel::vfs::validate_path(target))
        {
            return false;
        }

        Entry* entry = find_mount_by_target(target);
        if (entry == nullptr)
        {
            return false;
        }

        entry->source[0] = '\0';
        entry->target[0] = '\0';
        entry->active = false;
        return true;
    }

    bool resolve(const char* path, char* resolved, size_t resolved_capacity)
    {
        if (!g_ready || path == nullptr || resolved == nullptr || resolved_capacity == 0)
        {
            return false;
        }

        char aliased[MaxMountPathBytes];
        aliased[0] = '\0';
        if (!normalize_source_path(path, aliased, sizeof(aliased)))
        {
            return false;
        }

        if (!copy_string(resolved, resolved_capacity, aliased))
        {
            return false;
        }

        size_t best_index = MaxMounts;
        size_t best_length = 0;
        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (!g_mounts[index].active)
            {
                continue;
            }

            const size_t target_length = string_length(g_mounts[index].target);
            if (path_has_prefix(aliased, g_mounts[index].target) && target_length >= best_length)
            {
                best_index = index;
                best_length = target_length;
            }
        }

        if (best_index >= MaxMounts)
        {
            return true;
        }

        char rebuilt[MaxMountPathBytes];
        rebuilt[0] = '\0';
        if (!copy_string(rebuilt, sizeof(rebuilt), g_mounts[best_index].source))
        {
            return false;
        }

        if (aliased[best_length] != '\0')
        {
            const char* suffix = aliased + best_length;
            if (!append_suffix(rebuilt, sizeof(rebuilt), suffix))
            {
                return false;
            }
        }

        return copy_string(resolved, resolved_capacity, rebuilt);
    }

    bool is_mounted(const char* target)
    {
        return g_ready && target != nullptr && find_mount_by_target(target) != nullptr;
    }

    size_t active_count()
    {
        if (!g_ready)
        {
            return 0;
        }

        size_t count = 0;
        for (size_t index = 0; index < MaxMounts; ++index)
        {
            if (g_mounts[index].active)
            {
                ++count;
            }
        }

        return count;
    }

    bool active_at(size_t index, const char*& source, const char*& target)
    {
        source = nullptr;
        target = nullptr;
        if (!g_ready)
        {
            return false;
        }

        size_t current = 0;
        for (size_t mount_index = 0; mount_index < MaxMounts; ++mount_index)
        {
            if (!g_mounts[mount_index].active)
            {
                continue;
            }

            if (current == index)
            {
                source = g_mounts[mount_index].source;
                target = g_mounts[mount_index].target;
                return true;
            }

            ++current;
        }

        return false;
    }

    bool validation_self_test()
    {
        if (!g_ready || !tinyos::kernel::vfs::block_mount_ready())
        {
            return true;
        }

        char source[MaxMountPathBytes];
        if (!copy_string(source, sizeof(source), "/volumes/"))
        {
            return false;
        }

        const char* device_name = tinyos::kernel::vfs::blockfs::mounted_device_name();
        if (device_name == nullptr || device_name[0] == '\0' || !append_suffix(source, sizeof(source), device_name))
        {
            return false;
        }

        if (!is_block_volume_directory(source))
        {
            return false;
        }

        if (!mount(source, "/mnt"))
        {
            return false;
        }

        char resolved[MaxMountPathBytes];
        resolved[0] = '\0';
        if (!resolve("/mnt/volume.txt", resolved, sizeof(resolved)))
        {
            unmount("/mnt");
            return false;
        }

        char expected[MaxMountPathBytes];
        if (!copy_string(expected, sizeof(expected), source) ||
            !append_suffix(expected, sizeof(expected), "/volume.txt") ||
            !strings_equal(resolved, expected))
        {
            unmount("/mnt");
            return false;
        }

        const auto* mounted_file = tinyos::kernel::vfs::find("/mnt/volume.txt");
        if (mounted_file == nullptr || mounted_file->directory)
        {
            unmount("/mnt");
            return false;
        }

        resolved[0] = '\0';
        if (!resolve("/volumes/disk0/volume.txt", resolved, sizeof(resolved)) || !strings_equal(resolved, expected))
        {
            unmount("/mnt");
            return false;
        }

        return unmount("/mnt");
    }

    bool try_mount_layout_directory(const char* directory_name, const char* target)
    {
        if (directory_name == nullptr || target == nullptr || !tinyos::kernel::vfs::blockfs::has_layout_directory(directory_name))
        {
            return false;
        }

        char source[MaxMountPathBytes];
        source[0] = '\0';
        if (!copy_string(source, sizeof(source), "/volumes/disk0") || !append_suffix(source, sizeof(source), directory_name))
        {
            return false;
        }

        if (mount_known_block_directory(source, target))
        {
            return true;
        }

        kernel::klog::write_line(kernel::klog::Level::Warn, "Persistent layout bind failed.");
        return false;
    }

    void auto_mount_persistent_layout()
    {
        if (!g_ready || !tinyos::kernel::vfs::block_mount_ready())
        {
            return;
        }

        bool mounted_any = false;
        if (try_mount_layout_directory("system", "/system"))
        {
            mounted_any = true;
        }

        if (try_mount_layout_directory("users", "/users"))
        {
            mounted_any = true;
        }

        if (try_mount_layout_directory("apps", "/apps"))
        {
            mounted_any = true;
        }

        if (mounted_any)
        {
            kernel::klog::write_line(kernel::klog::Level::Info, "Persistent layout mounted.");
        }
    }

    bool layout_validation_self_test()
    {
        if (!g_ready || !tinyos::kernel::vfs::block_mount_ready() || !tinyos::kernel::vfs::blockfs::has_layout_directory("system"))
        {
            return true;
        }

        const auto* profile = tinyos::kernel::vfs::find("/system/profile.txt");
        const char* profile_data = nullptr;
        size_t profile_size = 0;
        if (profile == nullptr || profile->directory || !tinyos::kernel::vfs::read_file(profile, profile_data, profile_size) || profile_data == nullptr || profile_size == 0)
        {
            return false;
        }

        constexpr char ProfilePrefix[] = "tinyos.profile.version=";
        for (size_t index = 0; ProfilePrefix[index] != '\0'; ++index)
        {
            if (index >= profile_size || profile_data[index] != ProfilePrefix[index])
            {
                return false;
            }
        }

        const auto* notes = tinyos::kernel::vfs::find("/users/notes.txt");
        const auto* example_tapp = tinyos::kernel::vfs::find("/apps/example-system-tool.tapp");
        return notes != nullptr && !notes->directory && example_tapp != nullptr && !example_tapp->directory;
    }
}
