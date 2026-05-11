#pragma once

#include <stddef.h>

namespace tinyos::kernel::klog
{
    enum class Level
    {
        Debug,
        Trace,
        Info,
        Warn,
        Error,
        Panic
    };

    enum class WarningCategory
    {
        Generic,
        Memory,
        Driver,
        Security,
        Runtime,
        Ui
    };

    void initialize();
    void write(Level level, const char* text);
    void write_line(Level level, const char* text);
    void write_warning(WarningCategory category, const char* text);
    size_t count(Level level);
    size_t warning_count();
    size_t warning_count(WarningCategory category);
    size_t error_count();
    const char* warning_category_name(WarningCategory category);
}
