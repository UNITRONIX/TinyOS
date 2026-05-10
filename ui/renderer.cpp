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
    uint64_t g_rejected_draw_calls = 0;

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
}

namespace tinyos::ui::renderer
{
    void initialize()
    {
        const auto* surface = tinyos::kernel::device::framebuffer::active_surface();
        if (surface == nullptr || !surface->ready)
        {
            g_state.surface_name = nullptr;
            g_state.backend = Backend::None;
            g_state.width = 0;
            g_state.height = 0;
            g_state.pitch = 0;
            g_state.cell_size = 0;
            g_state.text_output = false;
            g_state.pixel_output = false;
            g_state.ready = false;
            g_surface_address = 0;
            return;
        }

        g_state.surface_name = surface->name;
        g_state.width = surface->width;
        g_state.height = surface->height;
        g_state.pitch = surface->pitch;
        g_state.cell_size = surface->cell_size;
        g_surface_address = surface->address;

        if (surface->kind == tinyos::kernel::device::framebuffer::SurfaceKind::TextGrid)
        {
            g_state.backend = Backend::TextGrid;
            g_state.text_output = true;
            g_state.pixel_output = false;
            g_state.ready = surface->cell_size == 2;
            return;
        }

        if (surface->kind == tinyos::kernel::device::framebuffer::SurfaceKind::LinearFramebuffer)
        {
            g_state.backend = Backend::LinearFramebuffer;
            g_state.text_output = false;
            g_state.pixel_output = true;
            g_state.ready = false;
            return;
        }

        g_state.backend = Backend::None;
        g_state.text_output = false;
        g_state.pixel_output = false;
        g_state.ready = false;
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

    uint64_t draw_call_count()
    {
        return g_draw_calls;
    }

    uint64_t primitive_call_count()
    {
        return g_primitive_calls;
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