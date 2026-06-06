#include <stddef.h>
#include <stdint.h>

#include <tinyos/boot/multiboot.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>

namespace
{
    constexpr size_t MaxModules = 8;
    constexpr size_t MaxModuleNameLength = 128;
    constexpr size_t MaxVfsSegmentBytes = 48;
    constexpr char BootMountPath[] = "/boot";
    constexpr uint32_t FnvOffsetBasis = 2166136261u;
    constexpr uint32_t FnvPrime = 16777619u;
    constexpr const char* UnnamedModule = "unnamed";

    tinyos::kernel::initrd::modules::Module g_modules[MaxModules];
    size_t g_declared_module_count = 0;
    size_t g_module_count = 0;
    size_t g_rejected_module_count = 0;
    size_t g_truncated_module_count = 0;
    uint64_t g_total_bytes = 0;
    bool g_ready = false;
    bool g_validation_passed = false;

    tinyos::kernel::vfs::Node g_boot_root = { "boot", true, nullptr, nullptr, 0, 0, false, nullptr };
    tinyos::kernel::vfs::Node g_vfs_module_nodes[MaxModules];
    tinyos::kernel::vfs::Node* g_vfs_nodes[MaxModules + 1];
    size_t g_vfs_node_count = 0;
    size_t g_vfs_file_count = 0;
    bool g_vfs_mounted = false;

    bool range_within_known_memory(uint64_t begin, uint64_t size)
    {
        if (size == 0)
        {
            return false;
        }

        const uint64_t end = begin + size;
        if (end <= begin)
        {
            return false;
        }

        for (size_t index = 0; index < tinyos::kernel::memory::map::region_count(); ++index)
        {
            const auto& region = tinyos::kernel::memory::map::region(index);
            const uint64_t region_end = region.base + region.length;
            if (region.length == 0 || region_end <= region.base)
            {
                continue;
            }

            if (begin >= region.base && end <= region_end)
            {
                return true;
            }
        }

        return false;
    }

    bool module_payload_valid(const tinyos::boot::multiboot::ModuleEntry& entry)
    {
        if (entry.mod_start == 0 || entry.mod_end <= entry.mod_start)
        {
            return false;
        }

        return range_within_known_memory(entry.mod_start, static_cast<uint64_t>(entry.mod_end) - entry.mod_start);
    }

    bool module_name_valid(uint32_t address)
    {
        if (address == 0)
        {
            return true;
        }

        for (size_t offset = 0; offset < MaxModuleNameLength; ++offset)
        {
            const uint64_t current = static_cast<uint64_t>(address) + offset;
            if (!range_within_known_memory(current, 1))
            {
                return false;
            }

            const auto* character = reinterpret_cast<const char*>(static_cast<uintptr_t>(current));
            if (*character == '\0')
            {
                return true;
            }
        }

        return false;
    }

    uint32_t checksum_module(uint32_t start, uint32_t size)
    {
        uint32_t checksum = FnvOffsetBasis;
        const auto* bytes = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(start));

        for (uint32_t index = 0; index < size; ++index)
        {
            checksum ^= bytes[index];
            checksum *= FnvPrime;
        }

        return checksum;
    }

    void reject_module(const char* reason)
    {
        ++g_rejected_module_count;
        tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Warn, reason);
    }

    bool is_vfs_name_char(char value)
    {
        return (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') ||
            value == '-' ||
            value == '_' ||
            value == '.';
    }

    bool module_name_vfs_safe(const char* name)
    {
        if (name == nullptr || name[0] == '\0')
        {
            return false;
        }

        size_t length = 0;
        while (name[length] != '\0')
        {
            if (length >= MaxVfsSegmentBytes || !is_vfs_name_char(name[length]))
            {
                return false;
            }

            ++length;
        }

        return length != 0;
    }

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

    void register_vfs_node(tinyos::kernel::vfs::Node* node)
    {
        if (g_vfs_node_count < MaxModules + 1)
        {
            g_vfs_nodes[g_vfs_node_count++] = node;
        }
    }

    tinyos::kernel::vfs::Node* vfs_child_by_segment(tinyos::kernel::vfs::Node* parent, const char* segment, size_t length)
    {
        if (parent == nullptr || !parent->directory)
        {
            return nullptr;
        }

        for (size_t index = 0; index < g_vfs_node_count; ++index)
        {
            auto* node = g_vfs_nodes[index];
            if (node->parent == parent && name_equals_segment(node->name, segment, length))
            {
                return node;
            }
        }

        return nullptr;
    }

    const tinyos::kernel::initrd::modules::Module* module_for_node(const tinyos::kernel::vfs::Node* node)
    {
        if (node == nullptr || node->directory)
        {
            return nullptr;
        }

        for (size_t index = 0; index < g_module_count; ++index)
        {
            if (&g_vfs_module_nodes[index] == node)
            {
                return &g_modules[index];
            }
        }

        return nullptr;
    }
}

namespace tinyos::kernel::initrd::modules
{
    void initialize(uint32_t multiboot_info_addr)
    {
        g_declared_module_count = 0;
        g_module_count = 0;
        g_rejected_module_count = 0;
        g_truncated_module_count = 0;
        g_total_bytes = 0;
        g_ready = false;
        g_validation_passed = false;

        const auto* info = reinterpret_cast<const boot::multiboot::Info*>(multiboot_info_addr);
        if ((info->flags & boot::multiboot::FlagModules) == 0 || info->mods_count == 0)
        {
            kernel::klog::write_line(kernel::klog::Level::Info, "No boot modules present.");
            g_validation_passed = true;
            g_ready = true;
            kernel::klog::write_line(kernel::klog::Level::Info, "Boot module metadata validated.");
            return;
        }

        g_declared_module_count = info->mods_count;
        const uint64_t module_table_bytes = static_cast<uint64_t>(info->mods_count) * sizeof(boot::multiboot::ModuleEntry);
        if (info->mods_addr == 0 || !range_within_known_memory(info->mods_addr, module_table_bytes))
        {
            g_rejected_module_count = info->mods_count;
            g_ready = true;
            kernel::klog::write_line(kernel::klog::Level::Warn, "Boot module table metadata is invalid.");
            return;
        }

        const auto* module_entries = reinterpret_cast<const boot::multiboot::ModuleEntry*>(info->mods_addr);
        const size_t limit = info->mods_count < MaxModules ? info->mods_count : MaxModules;
        g_truncated_module_count = info->mods_count > MaxModules ? info->mods_count - MaxModules : 0;

        for (size_t index = 0; index < limit; ++index)
        {
            const auto& entry = module_entries[index];
            if (!module_payload_valid(entry))
            {
                reject_module("Boot module payload metadata is invalid.");
                continue;
            }

            if (!module_name_valid(entry.string))
            {
                reject_module("Boot module name metadata is invalid.");
                continue;
            }

            auto& module = g_modules[g_module_count];
            module.name = entry.string != 0 ? reinterpret_cast<const char*>(entry.string) : UnnamedModule;
            module.start = entry.mod_start;
            module.end = entry.mod_end;
            module.size = entry.mod_end - entry.mod_start;
            module.checksum = checksum_module(module.start, module.size);
            module.metadata_valid = true;
            module.name_valid = entry.string != 0;
            g_total_bytes += module.size;
            ++g_module_count;
        }

        g_validation_passed = g_rejected_module_count == 0 && g_truncated_module_count == 0;
        g_ready = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Boot modules parsed.");
        if (g_validation_passed)
        {
            kernel::klog::write_line(kernel::klog::Level::Info, "Boot module metadata validated.");
        }
        else
        {
            kernel::klog::write_line(kernel::klog::Level::Warn, "Boot module metadata validation rejected entries.");
        }
    }

    bool is_ready()
    {
        return g_ready;
    }

    bool validation_passed()
    {
        return g_validation_passed;
    }

    size_t declared_count()
    {
        return g_declared_module_count;
    }

    size_t count()
    {
        return g_module_count;
    }

    size_t rejected_count()
    {
        return g_rejected_module_count;
    }

    size_t truncated_count()
    {
        return g_truncated_module_count;
    }

    const Module* at(size_t index)
    {
        if (index >= g_module_count)
        {
            return nullptr;
        }

        return &g_modules[index];
    }

    uint64_t total_bytes()
    {
        return g_total_bytes;
    }

    void mount_vfs()
    {
        g_vfs_node_count = 0;
        g_vfs_file_count = 0;
        g_vfs_mounted = false;
        g_boot_root.parent = nullptr;

        if (!g_ready)
        {
            return;
        }

        register_vfs_node(&g_boot_root);
        for (size_t index = 0; index < g_module_count; ++index)
        {
            const auto& module = g_modules[index];
            if (!module.metadata_valid || !module_name_vfs_safe(module.name))
            {
                continue;
            }

            auto& node = g_vfs_module_nodes[index];
            node.name = module.name;
            node.directory = false;
            node.readonly_data = reinterpret_cast<const char*>(static_cast<uintptr_t>(module.start));
            node.writable_data = nullptr;
            node.size = module.size;
            node.capacity = 0;
            node.writable = false;
            node.parent = &g_boot_root;
            register_vfs_node(&node);
            ++g_vfs_file_count;
        }

        g_vfs_mounted = true;
        kernel::klog::write_line(kernel::klog::Level::Info, "Initrd boot modules mounted at /boot.");
    }

    bool vfs_ready()
    {
        return g_vfs_mounted;
    }

    size_t vfs_file_count()
    {
        return g_vfs_file_count;
    }

    const vfs::Node* vfs_root()
    {
        return g_vfs_mounted ? &g_boot_root : nullptr;
    }

    const vfs::Node* vfs_find(const char* path)
    {
        if (!g_vfs_mounted || path == nullptr)
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

        if (!name_equals_segment(g_boot_root.name, segment, length))
        {
            return nullptr;
        }

        auto* current = &g_boot_root;
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

            current = vfs_child_by_segment(current, segment, length);
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

    bool vfs_owns(const vfs::Node* node)
    {
        if (!g_vfs_mounted || node == nullptr)
        {
            return false;
        }

        for (size_t index = 0; index < g_vfs_node_count; ++index)
        {
            if (g_vfs_nodes[index] == node)
            {
                return true;
            }
        }

        return false;
    }

    size_t vfs_child_count(const vfs::Node* node)
    {
        if (!g_vfs_mounted || node == nullptr || !node->directory || !vfs_owns(node))
        {
            return 0;
        }

        size_t count = 0;
        for (size_t index = 0; index < g_vfs_node_count; ++index)
        {
            if (g_vfs_nodes[index]->parent == node)
            {
                ++count;
            }
        }

        return count;
    }

    const vfs::Node* vfs_child_at(const vfs::Node* node, size_t index)
    {
        if (!g_vfs_mounted || node == nullptr || !node->directory || !vfs_owns(node))
        {
            return nullptr;
        }

        size_t current_child = 0;
        for (size_t node_index = 0; node_index < g_vfs_node_count; ++node_index)
        {
            if (g_vfs_nodes[node_index]->parent != node)
            {
                continue;
            }

            if (current_child == index)
            {
                return g_vfs_nodes[node_index];
            }

            ++current_child;
        }

        return nullptr;
    }

    bool vfs_read_file(const vfs::Node* node, const char*& data, size_t& size)
    {
        data = nullptr;
        size = 0;
        if (!g_vfs_mounted || node == nullptr || node->directory || !vfs_owns(node))
        {
            return false;
        }

        const auto* module = module_for_node(node);
        if (module == nullptr || !module->metadata_valid || module->size == 0)
        {
            return false;
        }

        if (checksum_module(module->start, module->size) != module->checksum)
        {
            return false;
        }

        data = reinterpret_cast<const char*>(static_cast<uintptr_t>(module->start));
        size = module->size;
        return data != nullptr;
    }

    bool vfs_validation_self_test()
    {
        if (!g_vfs_mounted)
        {
            return false;
        }

        const auto* boot = vfs_find(BootMountPath);
        if (boot == nullptr || !boot->directory)
        {
            return false;
        }

        if (g_vfs_file_count == 0)
        {
            return g_module_count == 0;
        }

        if (vfs_child_count(boot) != g_vfs_file_count)
        {
            return false;
        }

        const auto* placeholder = vfs_find("/boot/initrd-placeholder");
        if (placeholder == nullptr || placeholder->directory)
        {
            return g_module_count == 0;
        }

        const char* data = nullptr;
        size_t data_size = 0;
        if (!vfs_read_file(placeholder, data, data_size) || data == nullptr || data_size == 0)
        {
            return false;
        }

        constexpr char ExpectedPrefix[] = "TinyOS initrd";
        for (size_t index = 0; ExpectedPrefix[index] != '\0'; ++index)
        {
            if (index >= data_size || data[index] != ExpectedPrefix[index])
            {
                return false;
            }
        }

        return true;
    }

    const char* vfs_mount_path()
    {
        return BootMountPath;
    }
}
