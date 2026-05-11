#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    bool g_initialized = false;
    size_t g_level_counts[6] = {};
    size_t g_warning_category_counts[6] = {};

    size_t level_index(tinyos::kernel::klog::Level level)
    {
        switch (level)
        {
        case tinyos::kernel::klog::Level::Debug:
            return 0;
        case tinyos::kernel::klog::Level::Trace:
            return 1;
        case tinyos::kernel::klog::Level::Info:
            return 2;
        case tinyos::kernel::klog::Level::Warn:
            return 3;
        case tinyos::kernel::klog::Level::Error:
            return 4;
        case tinyos::kernel::klog::Level::Panic:
            return 5;
        }

        return 0;
    }

    size_t warning_category_index(tinyos::kernel::klog::WarningCategory category)
    {
        switch (category)
        {
        case tinyos::kernel::klog::WarningCategory::Generic:
            return 0;
        case tinyos::kernel::klog::WarningCategory::Memory:
            return 1;
        case tinyos::kernel::klog::WarningCategory::Driver:
            return 2;
        case tinyos::kernel::klog::WarningCategory::Security:
            return 3;
        case tinyos::kernel::klog::WarningCategory::Runtime:
            return 4;
        case tinyos::kernel::klog::WarningCategory::Ui:
            return 5;
        }

        return 0;
    }

    const char* level_prefix(tinyos::kernel::klog::Level level)
    {
        switch (level)
        {
        case tinyos::kernel::klog::Level::Debug:
            return "[debug] ";
        case tinyos::kernel::klog::Level::Trace:
            return "[trace] ";
        case tinyos::kernel::klog::Level::Info:
            return "[info ] ";
        case tinyos::kernel::klog::Level::Warn:
            return "[warn ] ";
        case tinyos::kernel::klog::Level::Error:
            return "[error] ";
        case tinyos::kernel::klog::Level::Panic:
            return "[panic] ";
        }

        return "[log  ] ";
    }
}

namespace tinyos::kernel::klog
{
    void initialize()
    {
        for (size_t index = 0; index < 6; ++index)
        {
            g_level_counts[index] = 0;
            g_warning_category_counts[index] = 0;
        }

        g_initialized = true;
    }

    void write(Level level, const char* text)
    {
        if (!g_initialized)
        {
            return;
        }

        ++g_level_counts[level_index(level)];
        const char* prefix = level_prefix(level);
        drivers::vga::write(prefix);
        drivers::vga::write(text);
        drivers::serial::write(prefix);
        drivers::serial::write(text);
    }

    void write_line(Level level, const char* text)
    {
        write(level, text);
        drivers::vga::put_char('\n');
        drivers::serial::write_char('\n');
    }

    void write_warning(WarningCategory category, const char* text)
    {
        if (!g_initialized)
        {
            return;
        }

        ++g_warning_category_counts[warning_category_index(category)];
        write_line(Level::Warn, text);
    }

    size_t count(Level level)
    {
        return g_level_counts[level_index(level)];
    }

    size_t warning_count()
    {
        return count(Level::Warn);
    }

    size_t warning_count(WarningCategory category)
    {
        return g_warning_category_counts[warning_category_index(category)];
    }

    size_t error_count()
    {
        return count(Level::Error) + count(Level::Panic);
    }

    const char* warning_category_name(WarningCategory category)
    {
        switch (category)
        {
        case WarningCategory::Generic:
            return "generic";
        case WarningCategory::Memory:
            return "memory";
        case WarningCategory::Driver:
            return "driver";
        case WarningCategory::Security:
            return "security";
        case WarningCategory::Runtime:
            return "runtime";
        case WarningCategory::Ui:
            return "ui";
        }

        return "generic";
    }
}
