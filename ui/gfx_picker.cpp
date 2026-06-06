#include <stddef.h>
#include <stdint.h>

#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/kernel/vfs/vfs.hpp>
#include <tinyos/shell/completion.hpp>
#include <tinyos/ui/font_atlas.hpp>
#include <tinyos/ui/gfx_picker.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    constexpr size_t MaxEntries = 8;
    constexpr size_t EntryLength = 48;

    tinyos::ui::gfx_picker::Mode g_mode = tinyos::ui::gfx_picker::Mode::None;
    char g_entries[MaxEntries][EntryLength + 1];
    size_t g_entry_count = 0;
    size_t g_selected = 0;

    void clear_entries()
    {
        g_entry_count = 0;
        g_selected = 0;
        for (size_t index = 0; index < MaxEntries; ++index)
        {
            tinyos::core::memory::set(g_entries[index], 0, EntryLength + 1);
        }
    }

    void add_entry(const char* text)
    {
        if (text == nullptr || g_entry_count >= MaxEntries)
        {
            return;
        }

        (void)tinyos::core::memory::string_copy_safe(g_entries[g_entry_count], EntryLength + 1, text);
        ++g_entry_count;
    }

    void populate_mentions(const char* prefix, const char* working_directory)
    {
        (void)working_directory;
        clear_entries();
        const auto* node = tinyos::kernel::vfs::find("/");
        if (node == nullptr || !node->directory)
        {
            return;
        }

        const size_t count = tinyos::kernel::vfs::child_count(node);
        for (size_t index = 0; index < count && g_entry_count < MaxEntries; ++index)
        {
            const auto* child = tinyos::kernel::vfs::child_at(node, index);
            if (child == nullptr || child->name == nullptr)
            {
                continue;
            }

            if (prefix != nullptr && prefix[0] != '\0' && !tinyos::core::string::starts_with(child->name, prefix))
            {
                continue;
            }

            add_entry(child->name);
        }
    }

    void populate_commands(const char* prefix)
    {
        clear_entries();
        const size_t count = tinyos::shell::completion::command_count();
        for (size_t index = 0; index < count && g_entry_count < MaxEntries; ++index)
        {
            const char* command = tinyos::shell::completion::command_at(index);
            if (command == nullptr)
            {
                continue;
            }

            if (prefix != nullptr && prefix[0] != '\0' && !tinyos::core::string::starts_with(command, prefix))
            {
                continue;
            }

            add_entry(command);
        }
    }
}

namespace tinyos::ui::gfx_picker
{
    void initialize()
    {
        g_mode = tinyos::ui::gfx_picker::Mode::None;
        clear_entries();
    }

    Mode mode()
    {
        return g_mode;
    }

    void cycle_mode()
    {
        switch (g_mode)
        {
        case tinyos::ui::gfx_picker::Mode::None:
            g_mode = tinyos::ui::gfx_picker::Mode::Mention;
            break;
        case tinyos::ui::gfx_picker::Mode::Mention:
            g_mode = tinyos::ui::gfx_picker::Mode::Command;
            break;
        case tinyos::ui::gfx_picker::Mode::Command:
            g_mode = tinyos::ui::gfx_picker::Mode::None;
            clear_entries();
            break;
        }
    }

    void update_for_input(const char* input, size_t cursor, const char* working_directory)
    {
        if (input == nullptr || cursor == 0)
        {
            if (g_mode == tinyos::ui::gfx_picker::Mode::None)
            {
                clear_entries();
            }

            return;
        }

        if (input[cursor - 1] == '@')
        {
            g_mode = tinyos::ui::gfx_picker::Mode::Mention;
            populate_mentions("", working_directory);
            return;
        }

        if (input[cursor - 1] == '/')
        {
            g_mode = tinyos::ui::gfx_picker::Mode::Command;
            populate_commands("");
            return;
        }

        if (g_mode == tinyos::ui::gfx_picker::Mode::Mention)
        {
            size_t start = cursor;
            while (start > 0 && input[start - 1] != '@' && input[start - 1] != ' ')
            {
                --start;
            }

            char prefix[EntryLength + 1];
            size_t prefix_length = 0;
            for (size_t index = start; index < cursor && prefix_length < EntryLength; ++index)
            {
                prefix[prefix_length++] = input[index];
            }

            prefix[prefix_length] = '\0';
            populate_mentions(prefix, working_directory);
            return;
        }

        if (g_mode == tinyos::ui::gfx_picker::Mode::Command)
        {
            size_t start = cursor;
            while (start > 0 && input[start - 1] != '/')
            {
                --start;
            }

            char prefix[EntryLength + 1];
            size_t prefix_length = 0;
            for (size_t index = start; index < cursor && prefix_length < EntryLength; ++index)
            {
                prefix[prefix_length++] = input[index];
            }

            prefix[prefix_length] = '\0';
            populate_commands(prefix);
        }
    }

    void handle_key(char key)
    {
        if (g_entry_count == 0)
        {
            return;
        }

        if (key == tinyos::drivers::keyboard::KeyUp)
        {
            g_selected = g_selected == 0 ? g_entry_count - 1 : g_selected - 1;
        }
        else if (key == tinyos::drivers::keyboard::KeyDown)
        {
            g_selected = (g_selected + 1) % g_entry_count;
        }
    }

    bool draw(uint32_t x, uint32_t y, uint32_t width, uint32_t max_height, const gfx_theme::Theme& theme, uint32_t body_scale)
    {
        if (g_mode == tinyos::ui::gfx_picker::Mode::None || g_entry_count == 0)
        {
            return true;
        }

        const uint32_t row_height = tinyos::ui::font_atlas::GlyphHeight + 8;
        const uint32_t panel_height = row_height * static_cast<uint32_t>(g_entry_count) + 8;
        const uint32_t clipped_height = panel_height < max_height ? panel_height : max_height;
        bool ok = tinyos::ui::renderer::fill_pixels(x, y, width, clipped_height, theme.picker_bg);
        ok = tinyos::ui::renderer::fill_pixels(x, y, width, 2, theme.border) && ok;

        uint32_t row_y = y + 6;
        for (size_t index = 0; index < g_entry_count; ++index)
        {
            const auto color = index == g_selected ? theme.picker_selected : theme.foreground;
            if (index == g_selected)
            {
                ok = tinyos::ui::renderer::fill_pixels(x + 2, row_y - 2, width - 4, row_height - 2, theme.picker_selected) && ok;
                ok = tinyos::ui::font_atlas::draw_text(x + 8, row_y, g_entries[index], theme.foreground, tinyos::ui::font_atlas::Style::Normal) && ok;
            }
            else
            {
                ok = tinyos::ui::font_atlas::draw_text(x + 8, row_y, g_entries[index], color, tinyos::ui::font_atlas::Style::Normal) && ok;
            }

            row_y += row_height;
            if (row_y + row_height > y + clipped_height)
            {
                break;
            }
        }

        return ok;
    }

    bool validation_self_test()
    {
        initialize();
        populate_commands("he");
        return g_entry_count > 0;
    }
}
