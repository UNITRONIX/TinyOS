#pragma once

#include <stdint.h>

namespace tinyos::drivers::console
{
    enum class Backend : uint8_t
    {
        Vga,
        Serial
    };

    void initialize();

    Backend active_backend();
    void set_backend(Backend backend);

    // Mirroring duplicates console output to the serial port. It is enabled by
    // default: a machine whose only output is a VGA screen cannot be diagnosed
    // or scripted, and the previous build never called set_serial_mirror() at
    // all, so the mirror was dead code even though it existed.
    void set_serial_mirror(bool enabled);
    bool serial_mirror_enabled();

    void put_char(char character);
    void write(const char* text);
    void write_line(const char* text);

    // Blocks until a character arrives from any input source.
    //
    // Both the PS/2 keyboard and the serial port are polled, so one image works
    // on a desktop and headless under QEMU. Previously the shell read the
    // keyboard directly and the serial port had no input path at all.
    //
    // While waiting the console yields to the scheduler. Without that, the
    // preemption watchdog counted the idle wait as a 500-tick overrun and
    // printed "Task watchdog: task exceeded time slice without yield" on every
    // keystroke pause.
    char read_char();

    // Non-blocking probe of the same sources. Returns false when nothing is
    // pending.
    bool try_read_char(char& character);

    // Source that produced the most recent character, for diagnostics.
    enum class InputSource : uint8_t
    {
        None,
        Keyboard,
        Serial
    };

    InputSource last_input_source();
    uint64_t keyboard_input_count();
    uint64_t serial_input_count();

    bool validation_self_test();
}
