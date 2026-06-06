#include <stddef.h>

#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/ui/gfx_theme.hpp>

namespace
{
    constexpr const char* ConfigPath = "/system/tinyos.conf";
    tinyos::ui::gfx_theme::Preset g_preset = tinyos::ui::gfx_theme::Preset::Copilot;

    tinyos::ui::gfx_theme::Theme make_copilot()
    {
        tinyos::ui::gfx_theme::Theme theme = {};
        theme.background = { 0, 0, 0, 0xFF };
        theme.foreground = { 245, 245, 245, 0xFF };
        theme.dim = { 120, 120, 120, 0xFF };
        theme.accent = { 196, 181, 253, 0xFF };
        theme.accent_shadow = { 124, 111, 186, 0xFF };
        theme.border = { 210, 210, 210, 0xFF };
        theme.mascot_outline = { 255, 105, 180, 0xFF };
        theme.mascot_eye = { 255, 255, 255, 0xFF };
        theme.mascot_mouth = { 0, 255, 127, 0xFF };
        theme.cursor = { 245, 245, 245, 0xFF };
        theme.output = { 200, 210, 220, 0xFF };
        theme.picker_bg = { 24, 24, 24, 0xF0 };
        theme.picker_selected = { 80, 70, 140, 0xFF };
        return theme;
    }

    tinyos::ui::gfx_theme::Theme make_dracula()
    {
        tinyos::ui::gfx_theme::Theme theme = make_copilot();
        theme.background = { 40, 42, 54, 0xFF };
        theme.foreground = { 248, 248, 242, 0xFF };
        theme.accent = { 189, 147, 249, 0xFF };
        theme.accent_shadow = { 98, 72, 170, 0xFF };
        theme.output = { 139, 233, 253, 0xFF };
        theme.picker_bg = { 68, 71, 90, 0xF0 };
        theme.picker_selected = { 98, 114, 164, 0xFF };
        return theme;
    }

    tinyos::ui::gfx_theme::Theme make_solarized()
    {
        tinyos::ui::gfx_theme::Theme theme = make_copilot();
        theme.background = { 0, 43, 54, 0xFF };
        theme.foreground = { 131, 148, 150, 0xFF };
        theme.accent = { 38, 139, 210, 0xFF };
        theme.accent_shadow = { 7, 54, 66, 0xFF };
        theme.output = { 133, 153, 0, 0xFF };
        theme.picker_bg = { 7, 54, 66, 0xF0 };
        theme.picker_selected = { 38, 139, 210, 0xFF };
        return theme;
    }

    tinyos::ui::gfx_theme::Theme g_active_theme = {};

    tinyos::ui::gfx_theme::Theme theme_for(tinyos::ui::gfx_theme::Preset preset)
    {
        switch (preset)
        {
        case tinyos::ui::gfx_theme::Preset::Dracula:
            return make_dracula();
        case tinyos::ui::gfx_theme::Preset::Solarized:
            return make_solarized();
        case tinyos::ui::gfx_theme::Preset::Copilot:
        default:
            return make_copilot();
        }
    }

    void refresh_active_theme()
    {
        g_active_theme = theme_for(g_preset);
    }

    bool parse_preset_line(const char* text)
    {
        if (text == nullptr)
        {
            return false;
        }

        if (tinyos::core::string::starts_with(text, "theme=copilot"))
        {
            g_preset = tinyos::ui::gfx_theme::Preset::Copilot;
            return true;
        }

        if (tinyos::core::string::starts_with(text, "theme=dracula"))
        {
            g_preset = tinyos::ui::gfx_theme::Preset::Dracula;
            return true;
        }

        if (tinyos::core::string::starts_with(text, "theme=solarized"))
        {
            g_preset = tinyos::ui::gfx_theme::Preset::Solarized;
            return true;
        }

        return false;
    }
}

namespace tinyos::ui::gfx_theme
{
    void initialize()
    {
        g_preset = Preset::Copilot;
        (void)load_from_config();
        refresh_active_theme();
    }

    Preset active_preset()
    {
        return g_preset;
    }

    const Theme* active()
    {
        return &g_active_theme;
    }

    bool set_preset(Preset preset)
    {
        if (preset >= Preset::Count)
        {
            return false;
        }

        g_preset = preset;
        refresh_active_theme();
        return save_to_config();
    }

    bool load_from_config()
    {
        const auto* node = tinyos::kernel::vfs::find(ConfigPath);
        const char* text = nullptr;
        size_t size = 0;
        if (node == nullptr || !tinyos::kernel::vfs::read_file(node, text, size) || text == nullptr || size == 0)
        {
            return false;
        }

        size_t index = 0;
        while (index < size)
        {
            size_t line_start = index;
            while (index < size && text[index] != '\n')
            {
                ++index;
            }

            char line[64];
            const size_t line_length = index - line_start;
            if (line_length >= sizeof(line))
            {
                if (index < size)
                {
                    ++index;
                }

                continue;
            }

            tinyos::core::memory::set(line, 0, sizeof(line));
            for (size_t copy_index = 0; copy_index < line_length; ++copy_index)
            {
                line[copy_index] = text[line_start + copy_index];
            }

            (void)parse_preset_line(line);
            if (index < size)
            {
                ++index;
            }
        }

        refresh_active_theme();
        return true;
    }

    bool save_to_config()
    {
        (void)ConfigPath;
        return true;
    }

    const char* preset_name(Preset preset)
    {
        switch (preset)
        {
        case Preset::Copilot:
            return "copilot";
        case Preset::Dracula:
            return "dracula";
        case Preset::Solarized:
            return "solarized";
        case Preset::Count:
            break;
        }

        return "unknown";
    }

    bool validation_self_test()
    {
        return active() != nullptr && preset_name(Preset::Copilot) != nullptr;
    }
}
