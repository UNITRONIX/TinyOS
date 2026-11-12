#pragma once

#include <stddef.h>

namespace tinyos::shell
{
    // Interactive shell entry point. Initialises the command registry and runs
    // the read/execute loop forever.
    [[noreturn]] void run();

    // Executes one already-assembled command line against the current session.
    // Exposed so the QEMU test harness can drive the shell without a keyboard.
    void execute(const char* input);

    // Registers command tables and validates the registry. Called before the
    // first prompt.
    void initialize();

    // Selects where interactive input comes from. Defaults to the PS/2 keyboard.
    // A headless image sets Serial, which is what makes the QEMU shell test
    // possible (defect D18).
    void set_serial_input(bool enabled);
    bool serial_input_enabled();

    // Number of lines executed since boot. Used by diagnostics and tests.
    size_t lines_executed();
}
