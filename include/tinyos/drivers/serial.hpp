#pragma once

namespace tinyos::drivers::serial
{
    void initialize();
    void write_char(char character);
    void write(const char* text);
    void write_line(const char* text);

    // Reads one received byte without blocking. Returns false when the receive
    // buffer is empty.
    //
    // Receive support exists because a headless console had no input path at
    // all: the console could only be written to, so the shell was impossible to
    // drive without a PS/2 keyboard (defect D18). The QEMU shell test harness
    // depends on this.
    bool try_read_char(char& character);

    // Blocks until a byte arrives. Used by the shell when the console input
    // source is serial.
    char read_char();

    // Number of bytes dropped because the caller did not drain them in time.
    unsigned long dropped_input_count();
}
