#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/vfs/blockfs.hpp>
#include <tinyos/kernel/vfs/ramfs.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>

namespace
{
    constexpr size_t MaxPathBytes = 128;
    constexpr size_t MaxSegmentBytes = 48;

    bool g_ready = false;

    bool is_allowed_path_char(char value)
    {
        return (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '-' ||
            value == '_' ||
            value == '.';
    }

    bool is_dot_segment(const char* path, size_t segment_start, size_t segment_length)
    {
        return (segment_length == 1 && path[segment_start] == '.') ||
            (segment_length == 2 && path[segment_start] == '.' && path[segment_start + 1] == '.');
    }

    bool segment_ok(const char* path, size_t segment_start, size_t segment_length)
    {
        return segment_length != 0 && segment_length <= MaxSegmentBytes && !is_dot_segment(path, segment_start, segment_length);
    }
}

namespace tinyos::kernel::vfs
{
    void initialize()
    {
        ramfs::initialize();
        blockfs::initialize();
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "VFS scaffold initialized.");
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool block_mount_ready()
    {
        return g_ready && blockfs::is_ready();
    }

    bool validate_path(const char* path)
    {
        if (path == nullptr || path[0] != '/')
        {
            return false;
        }

        if (path[1] == '\0')
        {
            return true;
        }

        size_t index = 1;
        size_t segment_start = 1;
        size_t segment_length = 0;
        while (path[index] != '\0')
        {
            if (index >= MaxPathBytes)
            {
                return false;
            }

            if (path[index] == '/')
            {
                if (!segment_ok(path, segment_start, segment_length))
                {
                    return false;
                }

                segment_start = index + 1;
                segment_length = 0;
                ++index;
                continue;
            }

            if (!is_allowed_path_char(path[index]))
            {
                return false;
            }

            ++segment_length;
            if (segment_length > MaxSegmentBytes)
            {
                return false;
            }

            ++index;
        }

        return segment_ok(path, segment_start, segment_length);
    }

    bool validation_self_test()
    {
        return validate_path("/") &&
            validate_path("/users/notes.txt") &&
            validate_path("/volumes/ram-block0/volume.txt") &&
            !validate_path(nullptr) &&
            !validate_path("") &&
            !validate_path("users/notes.txt") &&
            !validate_path("/users/../system/tapp.txt") &&
            !validate_path("/users/./notes.txt") &&
            !validate_path("/users//notes.txt") &&
            !validate_path("/users/notes.txt/") &&
            !validate_path("/users/notes.txt;reboot");
    }

    const Node* root()
    {
        return g_ready ? ramfs::root() : nullptr;
    }

    const Node* find(const char* path)
    {
        if (!g_ready || !validate_path(path))
        {
            return nullptr;
        }

        const auto* block_node = blockfs::find(path);
        return block_node != nullptr ? block_node : ramfs::find(path);
    }

    size_t child_count(const Node* node)
    {
        if (!g_ready || !can_list_directory(node))
        {
            return 0;
        }

        if (node == ramfs::root())
        {
            return ramfs::child_count(node) + (blockfs::is_ready() ? 1 : 0);
        }

        return blockfs::owns(node) ? blockfs::child_count(node) : ramfs::child_count(node);
    }

    const Node* child_at(const Node* node, size_t index)
    {
        if (!g_ready || !can_list_directory(node))
        {
            return nullptr;
        }

        if (node == ramfs::root())
        {
            const size_t ramfs_count = ramfs::child_count(node);
            if (index < ramfs_count)
            {
                return ramfs::child_at(node, index);
            }

            return index == ramfs_count ? blockfs::root() : nullptr;
        }

        return blockfs::owns(node) ? blockfs::child_at(node, index) : ramfs::child_at(node, index);
    }

    bool read_file(const Node* node, const char*& data, size_t& size)
    {
        if (!g_ready)
        {
            data = nullptr;
            size = 0;
            return false;
        }

        return blockfs::owns(node) ? blockfs::read_file(node, data, size) : ramfs::read_file(node, data, size);
    }

    bool write_file(const char* path, const char* data, size_t size)
    {
        if (!g_ready || !validate_path(path))
        {
            return false;
        }

        return blockfs::find(path) != nullptr ? blockfs::write_file(path, data, size) : ramfs::write_file(path, data, size);
    }

    bool create_directory(const char* path)
    {
        if (!g_ready || !validate_path(path) || blockfs::find(path) != nullptr)
        {
            return false;
        }

        return ramfs::create_directory(path);
    }

    bool create_file(const char* path)
    {
        if (!g_ready || !validate_path(path) || blockfs::find(path) != nullptr)
        {
            return false;
        }

        return ramfs::create_file(path);
    }

    bool remove(const char* path)
    {
        if (!g_ready || !validate_path(path) || blockfs::find(path) != nullptr)
        {
            return false;
        }

        return ramfs::remove(path);
    }

    bool copy_file(const char* source_path, const char* destination_path)
    {
        if (!g_ready || !validate_path(source_path) || !validate_path(destination_path) || blockfs::find(source_path) != nullptr || blockfs::find(destination_path) != nullptr)
        {
            return false;
        }

        return ramfs::copy_file(source_path, destination_path);
    }

    bool move(const char* source_path, const char* destination_path)
    {
        if (!g_ready || !validate_path(source_path) || !validate_path(destination_path) || blockfs::find(source_path) != nullptr || blockfs::find(destination_path) != nullptr)
        {
            return false;
        }

        return ramfs::move(source_path, destination_path);
    }

    uint16_t access_mode(const Node* node)
    {
        if (!g_ready || node == nullptr)
        {
            return 0;
        }

        if (blockfs::owns(node))
        {
            return node->directory ? 0555 : 0444;
        }

        return ramfs::access_mode(node);
    }

    bool can_enter_directory(const Node* node)
    {
        return g_ready && node != nullptr && node->directory && (access_mode(node) & 0100) == 0100;
    }

    bool can_list_directory(const Node* node)
    {
        return g_ready && node != nullptr && node->directory && (access_mode(node) & 0500) == 0500;
    }

    bool can_modify_directory(const Node* node)
    {
        return g_ready && node != nullptr && node->directory && (access_mode(node) & 0300) == 0300;
    }

    bool set_access_mode(const char* path, uint16_t mode)
    {
        if (!g_ready || !validate_path(path) || mode > 0777 || blockfs::find(path) != nullptr)
        {
            return false;
        }

        return ramfs::set_access_mode(path, mode);
    }
}
