#include <stddef.h>
#include <stdint.h>

#include <tinyos/boot/multiboot.hpp>
#include <tinyos/kernel/initrd/modules.hpp>
#include <tinyos/kernel/klog.hpp>
#include <tinyos/kernel/memory/memory_map.hpp>

namespace
{
    constexpr size_t MaxModules = 8;
    constexpr size_t MaxModuleNameLength = 128;
    constexpr uint32_t FnvOffsetBasis = 2166136261u;
    constexpr uint32_t FnvPrime = 16777619u;
    constexpr const char* UnnamedModule = "unnamed";

    tinyos::kernel::initrd::modules::Module g_modules[MaxModules] = {};
    size_t g_declared_module_count = 0;
    size_t g_module_count = 0;
    size_t g_rejected_module_count = 0;
    size_t g_truncated_module_count = 0;
    uint64_t g_total_bytes = 0;
    bool g_ready = false;
    bool g_validation_passed = false;

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
}
