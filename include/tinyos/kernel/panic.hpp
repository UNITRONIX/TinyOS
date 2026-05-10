#pragma once

#include <tinyos/kernel/klog.hpp>

namespace tinyos::kernel
{
    [[noreturn]] void panic(const char* message);
}

#define TINYOS_ASSERT(condition, message) \
    do \
    { \
        if (!(condition)) \
        { \
            ::tinyos::kernel::panic(message); \
        } \
    } while (false)

#define TINYOS_WARN_ON(condition, message) \
    do \
    { \
        if (condition) \
        { \
            ::tinyos::kernel::klog::write_line(::tinyos::kernel::klog::Level::Warn, message); \
        } \
    } while (false)
