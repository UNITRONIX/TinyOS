#pragma once

namespace tinyos::shell
{
    [[noreturn]] void run();
    void execute(const char* input);
}
