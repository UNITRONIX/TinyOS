// Capture stream, kept free of driver dependencies.
//
// The device-backed streams (console/serial/vga) live in libshell/stream.cpp
// because they call driver code. The capture stream does not, so it lives here:
// host tests can link it without pulling in port I/O, and it is the natural
// building block for pipelines.

#include <tinyos/libshell/stream.hpp>

namespace
{
    using tinyos::libshell::Stream;

    void buffer_write(Stream* self, const char* text, size_t length)
    {
        auto* state = static_cast<tinyos::libshell::BufferState*>(self->state);
        if (state == nullptr)
        {
            return;
        }

        for (size_t index = 0; index < length; ++index)
        {
            if (state->buffer == nullptr || state->capacity == 0)
            {
                state->truncated = true;
                continue;
            }

            // Reserve one byte for the terminator so the buffer is always a
            // valid C string, and report truncation instead of silently losing
            // bytes (defect D12).
            if (state->size + 1 >= state->capacity)
            {
                state->truncated = true;
                continue;
            }

            state->buffer[state->size] = text[index];
            ++state->size;
            state->buffer[state->size] = '\0';
        }
    }

    void null_write(Stream*, const char*, size_t)
    {
    }
}

namespace tinyos::libshell
{
    Stream null_stream()
    {
        Stream stream = {};
        stream.write_bytes = null_write;
        stream.state = nullptr;
        return stream;
    }

    Stream buffer_stream(BufferState& state, char* buffer, size_t capacity)
    {
        state.buffer = buffer;
        state.capacity = capacity;
        state.size = 0;
        state.truncated = false;

        if (buffer != nullptr && capacity > 0)
        {
            buffer[0] = '\0';
        }

        Stream stream = {};
        stream.write_bytes = buffer_write;
        stream.state = &state;
        return stream;
    }
}
