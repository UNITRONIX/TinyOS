#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinyos::libshell
{
    // Output sink for shell commands.
    //
    // The whole shell used to call tinyos::drivers::vga:: directly (1499 call
    // sites). That hard-wired the shell to VGA text mode, which is x86 PC
    // specific, and made the shell impossible to test without a screen or to run
    // over a serial console. A command now receives a Stream and never learns
    // which device is behind it.
    //
    // This is a plain struct of function pointers rather than a class with
    // virtual functions. A virtual interface pulls in a vtable plus
    // operator delete, and function-local statics pull in __cxa_guard_acquire;
    // none of those exist in a freestanding kernel, and the first attempt to
    // build this way failed at link time with exactly those symbols.
    struct Stream
    {
        // Writes `length` bytes. Every stream must accept a null `text` with
        // length 0 as a no-op.
        void (*write_bytes)(Stream* self, const char* text, size_t length);

        // Implementation state (device selector, buffer cursor, ...). May be
        // null for stateless streams.
        void* state;

        // NUL-terminated convenience wrapper.
        void write(const char* text) const
        {
            if (text == nullptr || write_bytes == nullptr)
            {
                return;
            }

            size_t length = 0;
            while (text[length] != '\0')
            {
                ++length;
            }

            write_bytes(const_cast<Stream*>(this), text, length);
        }

        void write(const char* text, size_t length) const
        {
            if (write_bytes == nullptr || text == nullptr)
            {
                return;
            }

            write_bytes(const_cast<Stream*>(this), text, length);
        }

        void put_char(char character) const
        {
            if (write_bytes == nullptr)
            {
                return;
            }

            write_bytes(const_cast<Stream*>(this), &character, 1);
        }

        void line(const char* text) const
        {
            write(text);
            put_char('\n');
        }
    };

    // Writes to the active text console (VGA plus optional serial mirror).
    Stream console_stream();

    // Writes only to the serial port. Used for headless operation and by the
    // QEMU shell test harness.
    Stream serial_stream();

    // Writes only to the VGA text buffer, with no serial mirror.
    Stream vga_stream();

    // Discards everything. Used where a command's output is irrelevant but its
    // exit code matters.
    Stream null_stream();

    // Capture state for BufferStream. Lives in caller storage so no allocation
    // is needed and the object has no destructor to register at exit.
    struct BufferState
    {
        char* buffer;
        size_t capacity;
        size_t size;
        bool truncated;
    };

    // Prepares `state` and returns a Stream writing into `buffer`. The buffer is
    // always NUL-terminated, so a truncated capture is still a valid string and
    // the caller can detect truncation through state.truncated.
    Stream buffer_stream(BufferState& state, char* buffer, size_t capacity);
}
