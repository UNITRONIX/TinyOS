#include <tinyos/drivers/console.hpp>
#include <tinyos/drivers/serial.hpp>
#include <tinyos/drivers/vga.hpp>
#include <tinyos/libshell/stream.hpp>

namespace
{
    using tinyos::libshell::Stream;

    // Console output: the active text console, which is VGA plus an optional
    // serial mirror. This is the default for interactive sessions.
    void console_write(Stream*, const char* text, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            tinyos::drivers::console::put_char(text[index]);
        }
    }

    // Serial-only output. This is what makes a headless session possible: the
    // shell can run with no VGA present, which is required before aarch64 and
    // which the QEMU shell test harness depends on.
    void serial_write(Stream*, const char* text, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            tinyos::drivers::serial::write_char(text[index]);
        }
    }

    // VGA only, bypassing the console layer and its serial mirror. vga::write()
    // and vga::put_char() deliberately delegate to the console (see
    // drivers/vga.cpp), so they are not a hardware-only path; the hardware_*
    // entry points are.
    void vga_write(Stream*, const char* text, size_t length)
    {
        for (size_t index = 0; index < length; ++index)
        {
            tinyos::drivers::vga::hardware_put_char(text[index]);
        }
    }

    // The device streams carry no state, so they are plain aggregates built on
    // every call. Returning by value avoids both static initialisation and the
    // __cxa_guard machinery that function-local statics would require.
    Stream make_console()
    {
        Stream stream = {};
        stream.write_bytes = console_write;
        stream.state = nullptr;
        return stream;
    }

    Stream make_serial()
    {
        Stream stream = {};
        stream.write_bytes = serial_write;
        stream.state = nullptr;
        return stream;
    }

    Stream make_vga()
    {
        Stream stream = {};
        stream.write_bytes = vga_write;
        stream.state = nullptr;
        return stream;
    }
}

namespace tinyos::libshell
{
    Stream console_stream()
    {
        return make_console();
    }

    Stream serial_stream()
    {
        return make_serial();
    }

    Stream vga_stream()
    {
        return make_vga();
    }
}
