#pragma once

#include <stdint.h>

#include <tinyos/ui/gfx_theme.hpp>

namespace tinyos::ui::gfx_picker
{
    enum class Mode : uint8_t
    {
        None,
        Mention,
        Command
    };

    void initialize();
    Mode mode();
    void cycle_mode();
    void update_for_input(const char* input, size_t cursor, const char* working_directory);
    void handle_key(char key);
    bool draw(uint32_t x, uint32_t y, uint32_t width, uint32_t max_height, const gfx_theme::Theme& theme, uint32_t body_scale);
    bool validation_self_test();
}
