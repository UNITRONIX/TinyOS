#include <stdint.h>

#include <tinyos/arch/io.hpp>
#include <tinyos/drivers/input.hpp>
#include <tinyos/drivers/mouse.hpp>

namespace
{
    constexpr uint16_t DataPort = 0x60;
    constexpr uint16_t StatusPort = 0x64;
    constexpr uint8_t StatusOutputFull = 0x01;
    constexpr uint8_t StatusInputFull = 0x02;
    constexpr uint8_t StatusAuxiliaryData = 0x20;
    constexpr uint8_t CommandReadConfig = 0x20;
    constexpr uint8_t CommandWriteConfig = 0x60;
    constexpr uint8_t CommandEnableAuxiliary = 0xA8;
    constexpr uint8_t CommandWriteAuxiliary = 0xD4;
    constexpr uint8_t MouseSetDefaults = 0xF6;
    constexpr uint8_t MouseEnableStreaming = 0xF4;
    constexpr uint8_t MouseAck = 0xFA;
    constexpr uint32_t DefaultWidth = 1024;
    constexpr uint32_t DefaultHeight = 768;

    bool g_ready = false;
    uint32_t g_width = DefaultWidth;
    uint32_t g_height = DefaultHeight;
    uint32_t g_cursor_x = DefaultWidth / 2;
    uint32_t g_cursor_y = DefaultHeight / 2;
    bool g_left_down = false;
    bool g_right_down = false;
    bool g_middle_down = false;
    uint8_t g_packet[3] = {};
    uint8_t g_packet_index = 0;
    uint64_t g_packets = 0;
    uint64_t g_button_events = 0;
    uint64_t g_dropped_packets = 0;

    bool wait_input_clear()
    {
        for (uint32_t attempt = 0; attempt < 100000; ++attempt)
        {
            if ((tinyos::arch::io::inb(StatusPort) & StatusInputFull) == 0)
            {
                return true;
            }

            tinyos::arch::io::io_wait();
        }

        return false;
    }

    bool wait_output_full()
    {
        for (uint32_t attempt = 0; attempt < 100000; ++attempt)
        {
            if ((tinyos::arch::io::inb(StatusPort) & StatusOutputFull) != 0)
            {
                return true;
            }

            tinyos::arch::io::io_wait();
        }

        return false;
    }

    bool write_controller(uint8_t value)
    {
        if (!wait_input_clear())
        {
            return false;
        }

        tinyos::arch::io::outb(StatusPort, value);
        return true;
    }

    bool write_data(uint8_t value)
    {
        if (!wait_input_clear())
        {
            return false;
        }

        tinyos::arch::io::outb(DataPort, value);
        return true;
    }

    bool read_data(uint8_t& value)
    {
        if (!wait_output_full())
        {
            return false;
        }

        value = tinyos::arch::io::inb(DataPort);
        return true;
    }

    bool write_mouse(uint8_t value)
    {
        return write_controller(CommandWriteAuxiliary) && write_data(value);
    }

    bool expect_ack()
    {
        uint8_t value = 0;
        return read_data(value) && value == MouseAck;
    }

    bool send_mouse_command(uint8_t value)
    {
        return write_mouse(value) && expect_ack();
    }

    int32_t sign_extend(uint8_t value)
    {
        return (value & 0x80) != 0 ? static_cast<int32_t>(value) - 256 : static_cast<int32_t>(value);
    }

    uint32_t clamp_position(int32_t value, uint32_t limit)
    {
        if (value < 0)
        {
            return 0;
        }

        if (limit == 0)
        {
            return 0;
        }

        const uint32_t unsigned_value = static_cast<uint32_t>(value);
        return unsigned_value >= limit ? limit - 1 : unsigned_value;
    }

    bool decode_packet(const uint8_t* packet, int32_t& delta_x, int32_t& delta_y, bool& left, bool& right, bool& middle)
    {
        if (packet == nullptr || (packet[0] & 0x08) == 0 || (packet[0] & 0xC0) != 0)
        {
            return false;
        }

        delta_x = sign_extend(packet[1]);
        delta_y = -sign_extend(packet[2]);
        left = (packet[0] & 0x01) != 0;
        right = (packet[0] & 0x02) != 0;
        middle = (packet[0] & 0x04) != 0;
        return true;
    }

    void push_button_if_changed(bool& stored, bool current, uint8_t button)
    {
        if (stored == current)
        {
            return;
        }

        stored = current;
        tinyos::drivers::input::push_mouse_button_event(g_cursor_x, g_cursor_y, button, current);
        ++g_button_events;
    }

    void process_packet(const uint8_t* packet)
    {
        int32_t delta_x = 0;
        int32_t delta_y = 0;
        bool left = false;
        bool right = false;
        bool middle = false;
        if (!decode_packet(packet, delta_x, delta_y, left, right, middle))
        {
            ++g_dropped_packets;
            return;
        }

        g_cursor_x = clamp_position(static_cast<int32_t>(g_cursor_x) + delta_x, g_width);
        g_cursor_y = clamp_position(static_cast<int32_t>(g_cursor_y) + delta_y, g_height);
        tinyos::drivers::input::push_pointer_event(g_cursor_x, g_cursor_y, delta_x, delta_y);
        push_button_if_changed(g_left_down, left, 1);
        push_button_if_changed(g_right_down, right, 2);
        push_button_if_changed(g_middle_down, middle, 3);
        ++g_packets;
    }
}

namespace tinyos::drivers::mouse
{
    void initialize()
    {
        g_ready = false;
        g_width = DefaultWidth;
        g_height = DefaultHeight;
        g_cursor_x = DefaultWidth / 2;
        g_cursor_y = DefaultHeight / 2;
        g_left_down = false;
        g_right_down = false;
        g_middle_down = false;
        g_packet_index = 0;
        g_packets = 0;
        g_button_events = 0;
        g_dropped_packets = 0;

        if (!write_controller(CommandEnableAuxiliary))
        {
            return;
        }

        if (!write_controller(CommandReadConfig))
        {
            return;
        }

        uint8_t config = 0;
        if (!read_data(config))
        {
            return;
        }

        config = static_cast<uint8_t>((config | 0x02) & ~0x20);
        if (!write_controller(CommandWriteConfig) || !write_data(config))
        {
            return;
        }

        if (!send_mouse_command(MouseSetDefaults) || !send_mouse_command(MouseEnableStreaming))
        {
            return;
        }

        g_ready = true;
    }

    bool is_ready()
    {
        return g_ready;
    }

    void set_bounds(uint32_t width, uint32_t height)
    {
        g_width = width == 0 ? DefaultWidth : width;
        g_height = height == 0 ? DefaultHeight : height;
        if (g_cursor_x >= g_width)
        {
            g_cursor_x = g_width - 1;
        }
        if (g_cursor_y >= g_height)
        {
            g_cursor_y = g_height - 1;
        }
    }

    void handle_irq()
    {
        const uint8_t status = tinyos::arch::io::inb(StatusPort);
        if ((status & (StatusOutputFull | StatusAuxiliaryData)) != (StatusOutputFull | StatusAuxiliaryData))
        {
            ++g_dropped_packets;
            return;
        }

        const uint8_t value = tinyos::arch::io::inb(DataPort);
        if (g_packet_index == 0 && (value & 0x08) == 0)
        {
            ++g_dropped_packets;
            return;
        }

        g_packet[g_packet_index] = value;
        ++g_packet_index;
        if (g_packet_index == 3)
        {
            g_packet_index = 0;
            process_packet(g_packet);
        }
    }

    uint32_t cursor_x()
    {
        return g_cursor_x;
    }

    uint32_t cursor_y()
    {
        return g_cursor_y;
    }

    bool left_button_down()
    {
        return g_left_down;
    }

    uint64_t packet_count()
    {
        return g_packets;
    }

    uint64_t button_event_count()
    {
        return g_button_events;
    }

    uint64_t dropped_packet_count()
    {
        return g_dropped_packets;
    }

    bool packet_decoder_self_test()
    {
        uint8_t packet[3];
        packet[0] = 0x09;
        packet[1] = 5;
        packet[2] = 253;
        int32_t dx = 0;
        int32_t dy = 0;
        bool left = false;
        bool right = true;
        bool middle = true;
        const bool decoded = decode_packet(packet, dx, dy, left, right, middle);
        return decoded && dx == 5 && dy == 3 && left && !right && !middle;
    }
}