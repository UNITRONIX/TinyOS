#include <tinyos/kernel/device/framebuffer.hpp>

namespace
{
    tinyos::kernel::device::framebuffer::Surface g_surface = {};
    bool g_ready = false;
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

    bool is_ready()
    {
        return g_ready && g_surface.ready;
    }

    const Surface* active_surface()
    {
        return is_ready() ? &g_surface : nullptr;
    }

    bool has_linear_framebuffer()
    {
        return is_ready() && g_surface.kind == SurfaceKind::LinearFramebuffer;
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