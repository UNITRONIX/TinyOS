#pragma once

#include <stdint.h>

#include <tinyos/ui/renderer.hpp>

namespace tinyos::ui::gfx_theme
{
    enum class Preset : uint8_t
    {
        Copilot,
        Dracula,
        Solarized,
        Count
    };

    struct Theme
    {
        renderer::Color background;
        renderer::Color foreground;
        renderer::Color dim;
        renderer::Color accent;
        renderer::Color accent_shadow;
        renderer::Color border;
        renderer::Color mascot_outline;
        renderer::Color mascot_eye;
        renderer::Color mascot_mouth;
        renderer::Color cursor;
        renderer::Color output;
        renderer::Color picker_bg;
        renderer::Color picker_selected;
    };

    void initialize();
    Preset active_preset();
    const Theme* active();
    bool set_preset(Preset preset);
    bool load_from_config();
    bool save_to_config();
    const char* preset_name(Preset preset);
    bool validation_self_test();
}
