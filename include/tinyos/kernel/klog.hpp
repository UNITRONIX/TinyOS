#pragma once

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

    void initialize();
    void write(Level level, const char* text);
    void write_line(Level level, const char* text);
}
