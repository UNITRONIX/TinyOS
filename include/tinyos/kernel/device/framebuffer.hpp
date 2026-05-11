#pragma once

#include <stdint.h>

namespace tinyos::kernel::device::framebuffer
{
    enum class SurfaceKind : uint32_t
    {
        TextGrid,
        LinearFramebuffer,
        Unknown
    };

    struct Surface
    {
        const char* name;
        SurfaceKind kind;
        uintptr_t address;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint32_t bits_per_pixel;
        uint32_t cell_size;
        bool ready;
    };

    void initialize_text_grid(const char* name, uint32_t columns, uint32_t rows, uintptr_t address, uint32_t cell_size);
    bool record_linear_framebuffer(const char* name, uintptr_t address, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bits_per_pixel);
    bool is_ready();
    const Surface* active_surface();
    const Surface* linear_surface();
    bool has_active_text_grid();
    bool has_linear_framebuffer();
    bool linear_framebuffer_contract_self_test();
    bool validation_self_test();
    const char* kind_name(SurfaceKind kind);
}