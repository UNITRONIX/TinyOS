#include <stddef.h>
#include <stdint.h>

#include <tinyos/kernel/device/framebuffer.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    tinyos::ui::renderer::State g_state = {};
    uintptr_t g_surface_address = 0;
    uint64_t g_draw_calls = 0;
    uint64_t g_primitive_calls = 0;
    uint64_t g_pixel_draw_calls = 0;
    uint64_t g_rejected_draw_calls = 0;
    uint32_t g_pixel_contract_buffer[4] = {};

    uint16_t make_text_entry(char character, uint8_t attribute)
    {
        return static_cast<uint16_t>(static_cast<uint8_t>(character)) | (static_cast<uint16_t>(attribute) << 8);
    }

    bool can_draw_text_at(uint32_t column, uint32_t row)
    {
        return g_state.ready &&
            g_state.backend == tinyos::ui::renderer::Backend::TextGrid &&
            g_surface_address != 0 &&
            g_state.cell_size == 2 &&
            column < g_state.width &&
            row < g_state.height;
    }

    bool can_draw_area_at(uint32_t column, uint32_t row, uint32_t width, uint32_t height)
    {
        return can_draw_text_at(column, row) && width != 0 && height != 0;
    }

    bool can_draw_pixel_at(uint32_t x, uint32_t y)
    {
        return g_state.ready &&
            g_state.backend == tinyos::ui::renderer::Backend::LinearFramebuffer &&
            g_state.pixel_output &&
            g_surface_address != 0 &&
            (g_state.bits_per_pixel == 24 || g_state.bits_per_pixel == 32) &&
            x < g_state.width &&
            y < g_state.height;
    }

    void write_rgb_pixel(uintptr_t base, uint32_t pitch, uint32_t bits_per_pixel, uint32_t x, uint32_t y, tinyos::ui::renderer::Color color)
    {
        const uint32_t offset = y * pitch + x * (bits_per_pixel / 8);
        auto* bytes = reinterpret_cast<volatile uint8_t*>(base + offset);
        bytes[0] = color.blue;
        bytes[1] = color.green;
        bytes[2] = color.red;
        if (bits_per_pixel == 32)
        {
            bytes[3] = color.alpha;
        }
    }
}

namespace tinyos::ui::renderer
{
    namespace
    {
        bool initialize_from_surface(const tinyos::kernel::device::framebuffer::Surface* surface)
        {
            if (surface == nullptr || !surface->ready)
            {
                g_state.surface_name = nullptr;
                g_state.backend = Backend::None;
                g_state.width = 0;
                g_state.height = 0;
                g_state.pitch = 0;
                g_state.bits_per_pixel = 0;
                g_state.cell_size = 0;
                g_state.text_output = false;
                g_state.pixel_output = false;
                g_state.ready = false;
                g_surface_address = 0;
                return false;
            }

            g_state.surface_name = surface->name;
            g_state.width = surface->width;
            g_state.height = surface->height;
            g_state.pitch = surface->pitch;
            g_state.bits_per_pixel = surface->bits_per_pixel;
            g_state.cell_size = surface->cell_size;
            g_surface_address = surface->address;

            if (surface->kind == tinyos::kernel::device::framebuffer::SurfaceKind::TextGrid)
            {
                g_state.backend = Backend::TextGrid;
                g_state.text_output = true;
                g_state.pixel_output = false;
                g_state.ready = surface->cell_size == 2;
                return g_state.ready;
            }

            if (surface->kind == tinyos::kernel::device::framebuffer::SurfaceKind::LinearFramebuffer)
            {
                g_state.backend = Backend::LinearFramebuffer;
                g_state.text_output = false;
                g_state.pixel_output = true;
                g_state.ready = surface->address != 0 && (surface->bits_per_pixel == 24 || surface->bits_per_pixel == 32);
                return g_state.ready;
            }

            g_state.backend = Backend::None;
            g_state.text_output = false;
            g_state.pixel_output = false;
            g_state.ready = false;
            return false;
        }
    }

    void initialize()
    {
        (void)initialize_from_surface(tinyos::kernel::device::framebuffer::active_surface());
    }

    bool initialize_linear_framebuffer()
    {
        return initialize_from_surface(tinyos::kernel::device::framebuffer::linear_surface());
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    bool draw_text(uint32_t column, uint32_t row, const char* text, uint8_t attribute)
    {
        if (text == nullptr || !can_draw_text_at(column, row))
        {
            ++g_rejected_draw_calls;
            return false;
        }

        auto* text_grid = reinterpret_cast<volatile uint16_t*>(g_surface_address);
        uint32_t current_column = column;
        size_t text_index = 0;
        while (text[text_index] != '\0' && text[text_index] != '\n' && current_column < g_state.width)
        {
            text_grid[(row * g_state.width) + current_column] = make_text_entry(text[text_index], attribute);
            ++current_column;
            ++text_index;
        }

        ++g_draw_calls;
        return true;
    }

    bool fill_rect(uint32_t column, uint32_t row, uint32_t width, uint32_t height, char fill, uint8_t attribute)
    {
        if (!can_draw_area_at(column, row, width, height))
        {
            ++g_rejected_draw_calls;
            return false;
        }

        auto* text_grid = reinterpret_cast<volatile uint16_t*>(g_surface_address);
        const uint32_t clipped_width = width < (g_state.width - column) ? width : (g_state.width - column);
        const uint32_t clipped_height = height < (g_state.height - row) ? height : (g_state.height - row);

        for (uint32_t row_offset = 0; row_offset < clipped_height; ++row_offset)
        {
            const uint32_t current_row = row + row_offset;
            for (uint32_t column_offset = 0; column_offset < clipped_width; ++column_offset)
            {
                const uint32_t current_column = column + column_offset;
                text_grid[(current_row * g_state.width) + current_column] = make_text_entry(fill, attribute);
            }
        }

        ++g_primitive_calls;
        return true;
    }

    bool clear_area(uint32_t column, uint32_t row, uint32_t width, uint32_t height, uint8_t attribute)
    {
        return fill_rect(column, row, width, height, ' ', attribute);
    }

    uint32_t pack_color(Color color)
    {
        return (static_cast<uint32_t>(color.alpha) << 24) |
            (static_cast<uint32_t>(color.red) << 16) |
            (static_cast<uint32_t>(color.green) << 8) |
            static_cast<uint32_t>(color.blue);
    }

    bool draw_pixel(uint32_t x, uint32_t y, Color color)
    {
        if (!can_draw_pixel_at(x, y))
        {
            ++g_rejected_draw_calls;
            return false;
        }

        write_rgb_pixel(g_surface_address, g_state.pitch, g_state.bits_per_pixel, x, y, color);
        ++g_pixel_draw_calls;
        return true;
    }

    bool fill_pixels(uint32_t x, uint32_t y, uint32_t width, uint32_t height, Color color)
    {
        if (!can_draw_pixel_at(x, y) || width == 0 || height == 0)
        {
            ++g_rejected_draw_calls;
            return false;
        }

        const uint32_t clipped_width = width < (g_state.width - x) ? width : (g_state.width - x);
        const uint32_t clipped_height = height < (g_state.height - y) ? height : (g_state.height - y);
        for (uint32_t row = 0; row < clipped_height; ++row)
        {
            for (uint32_t column = 0; column < clipped_width; ++column)
            {
                write_rgb_pixel(g_surface_address, g_state.pitch, g_state.bits_per_pixel, x + column, y + row, color);
            }
        }

        ++g_pixel_draw_calls;
        return true;
    }

    uint64_t draw_call_count()
    {
        return g_draw_calls;
    }

    uint64_t primitive_call_count()
    {
        return g_primitive_calls;
    }

    uint64_t pixel_draw_call_count()
    {
        return g_pixel_draw_calls;
    }

    uint64_t rejected_draw_call_count()
    {
        return g_rejected_draw_calls;
    }

    bool validation_self_test()
    {
        return g_state.ready &&
            g_state.surface_name != nullptr &&
            g_state.backend == Backend::TextGrid &&
            g_state.width >= 80 &&
            g_state.height >= 25 &&
            g_state.pitch >= g_state.width * g_state.cell_size &&
            g_state.cell_size == 2 &&
            g_state.text_output &&
            !g_state.pixel_output &&
            g_surface_address != 0;
    }

    bool primitive_validation_self_test()
    {
        return validation_self_test() &&
            can_draw_area_at(0, 0, 1, 1) &&
            can_draw_area_at(g_state.width - 1, g_state.height - 1, 1, 1);
    }

    bool pixel_contract_validation_self_test()
    {
        for (size_t index = 0; index < sizeof(g_pixel_contract_buffer) / sizeof(g_pixel_contract_buffer[0]); ++index)
        {
            g_pixel_contract_buffer[index] = 0;
        }

        const Color color = { 0x12, 0x34, 0x56, 0xFF };
        if (pack_color(color) != 0xFF123456)
        {
            return false;
        }

        write_rgb_pixel(reinterpret_cast<uintptr_t>(&g_pixel_contract_buffer[0]), 8, 32, 1, 1, color);
        return g_pixel_contract_buffer[3] == 0xFF123456 && tinyos::kernel::device::framebuffer::linear_framebuffer_contract_self_test();
    }

    const char* backend_name(Backend backend)
    {
        switch (backend)
        {
        case Backend::None:
            return "none";
        case Backend::TextGrid:
            return "text-grid";
        case Backend::LinearFramebuffer:
            return "linear-framebuffer";
        }

        return "unknown";
    }
}