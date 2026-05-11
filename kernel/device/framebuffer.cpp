#include <tinyos/kernel/device/framebuffer.hpp>

namespace
{
    tinyos::kernel::device::framebuffer::Surface g_surface = {};
    tinyos::kernel::device::framebuffer::Surface g_linear_surface = {};
    bool g_ready = false;
    bool g_linear_ready = false;
}

namespace tinyos::kernel::device::framebuffer
{
    void initialize_text_grid(const char* name, uint32_t columns, uint32_t rows, uintptr_t address, uint32_t cell_size)
    {
        g_surface.name = name;
        g_surface.kind = SurfaceKind::TextGrid;
        g_surface.address = address;
        g_surface.width = columns;
        g_surface.height = rows;
        g_surface.pitch = columns * cell_size;
        g_surface.bits_per_pixel = cell_size * 8;
        g_surface.cell_size = cell_size;
        g_surface.ready = name != nullptr && columns != 0 && rows != 0 && address != 0 && cell_size != 0;
        g_ready = g_surface.ready;
    }

    bool record_linear_framebuffer(const char* name, uintptr_t address, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bits_per_pixel)
    {
        const uint32_t bytes_per_pixel = bits_per_pixel / 8;
        const bool supported_depth = bits_per_pixel == 24 || bits_per_pixel == 32;
        const bool sane_pitch = bytes_per_pixel != 0 && pitch >= width * bytes_per_pixel;
        if (name == nullptr || address == 0 || width == 0 || height == 0 || !supported_depth || !sane_pitch)
        {
            g_linear_surface.ready = false;
            g_linear_ready = false;
            return false;
        }

        g_linear_surface.name = name;
        g_linear_surface.kind = SurfaceKind::LinearFramebuffer;
        g_linear_surface.address = address;
        g_linear_surface.width = width;
        g_linear_surface.height = height;
        g_linear_surface.pitch = pitch;
        g_linear_surface.bits_per_pixel = bits_per_pixel;
        g_linear_surface.cell_size = bytes_per_pixel;
        g_linear_surface.ready = true;
        g_linear_ready = true;
        return true;
    }

    bool is_ready()
    {
        return g_ready && g_surface.ready;
    }

    const Surface* active_surface()
    {
        return is_ready() ? &g_surface : nullptr;
    }

    const Surface* linear_surface()
    {
        return g_linear_ready && g_linear_surface.ready ? &g_linear_surface : nullptr;
    }

    bool has_active_text_grid()
    {
        return is_ready() && g_surface.kind == SurfaceKind::TextGrid;
    }

    bool has_linear_framebuffer()
    {
        return linear_surface() != nullptr;
    }

    bool linear_framebuffer_contract_self_test()
    {
        if (!has_linear_framebuffer())
        {
            return has_active_text_grid();
        }

        const auto* surface = linear_surface();
        const uint32_t bytes_per_pixel = surface->bits_per_pixel / 8;
        return surface->kind == SurfaceKind::LinearFramebuffer &&
            surface->address != 0 &&
            surface->width >= 320 &&
            surface->height >= 200 &&
            bytes_per_pixel != 0 &&
            surface->pitch >= surface->width * bytes_per_pixel &&
            surface->cell_size == bytes_per_pixel;
    }

    bool validation_self_test()
    {
        if (!is_ready())
        {
            return false;
        }

        if (g_surface.kind == SurfaceKind::Unknown || g_surface.name == nullptr)
        {
            return false;
        }

        if (g_surface.width == 0 || g_surface.height == 0 || g_surface.pitch == 0)
        {
            return false;
        }

        return g_surface.bits_per_pixel != 0 && g_surface.cell_size != 0;
    }

    const char* kind_name(SurfaceKind kind)
    {
        switch (kind)
        {
        case SurfaceKind::TextGrid:
            return "text-grid";
        case SurfaceKind::LinearFramebuffer:
            return "linear-framebuffer";
        case SurfaceKind::Unknown:
            return "unknown";
        }

        return "unknown";
    }
}