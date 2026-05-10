#pragma once

namespace tinyos::arch
{
    void initialize();
    [[noreturn]] void halt();
    [[noreturn]] void reboot();
}
