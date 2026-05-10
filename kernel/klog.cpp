#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/kernel/klog.hpp>

namespace
{
    bool g_initialized = false;

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
        g_initialized = true;
    }

    void write(Level level, const char* text)
    {
        if (!g_initialized)
        {
            return;
        }

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
}
