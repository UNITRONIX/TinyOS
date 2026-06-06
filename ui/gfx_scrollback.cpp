#include <stddef.h>
#include <stdint.h>

#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/ui/gfx_scrollback.hpp>

namespace
{
    char g_lines[tinyos::ui::gfx_scrollback::MaxLines][tinyos::ui::gfx_scrollback::MaxLineLength + 1];
    size_t g_line_count = 0;
    size_t g_current_column = 0;
    size_t g_scroll_offset = 0;

    void shift_lines_up()
    {
        for (size_t index = 1; index < tinyos::ui::gfx_scrollback::MaxLines; ++index)
        {
            (void)tinyos::core::memory::string_copy_safe(
                g_lines[index - 1],
                tinyos::ui::gfx_scrollback::MaxLineLength + 1,
                g_lines[index]);
        }

        tinyos::core::memory::set(g_lines[tinyos::ui::gfx_scrollback::MaxLines - 1], 0, tinyos::ui::gfx_scrollback::MaxLineLength + 1);
        if (g_line_count > 0)
        {
            g_line_count = tinyos::ui::gfx_scrollback::MaxLines - 1;
        }

        g_current_column = tinyos::core::string::length(g_lines[g_line_count]);
    }

    void ensure_line()
    {
        if (g_line_count >= tinyos::ui::gfx_scrollback::MaxLines)
        {
            shift_lines_up();
        }

        if (g_line_count == 0)
        {
            g_line_count = 1;
            tinyos::core::memory::set(g_lines[0], 0, tinyos::ui::gfx_scrollback::MaxLineLength + 1);
            g_current_column = 0;
        }
    }
}

namespace tinyos::ui::gfx_scrollback
{
    void initialize()
    {
        clear();
    }

    void clear()
    {
        g_line_count = 0;
        g_current_column = 0;
        g_scroll_offset = 0;
        for (size_t index = 0; index < tinyos::ui::gfx_scrollback::MaxLines; ++index)
        {
            tinyos::core::memory::set(g_lines[index], 0, tinyos::ui::gfx_scrollback::MaxLineLength + 1);
        }
    }

    void append_char(char character)
    {
        ensure_line();

        if (character == '\n')
        {
            if (g_line_count < MaxLines)
            {
                ++g_line_count;
            }
            else
            {
                shift_lines_up();
                ++g_line_count;
            }

            if (g_line_count < MaxLines)
            {
                tinyos::core::memory::set(g_lines[g_line_count], 0, MaxLineLength + 1);
            }

            g_current_column = 0;
            scroll_to_bottom();
            return;
        }

        if (character == '\r')
        {
            g_current_column = 0;
            return;
        }

        if (character == '\b')
        {
            if (g_current_column > 0)
            {
                --g_current_column;
                g_lines[g_line_count - 1][g_current_column] = '\0';
            }

            return;
        }

        if (g_current_column >= tinyos::ui::gfx_scrollback::MaxLineLength)
        {
            append_char('\n');
            ensure_line();
        }

        g_lines[g_line_count - 1][g_current_column] = character;
        ++g_current_column;
        g_lines[g_line_count - 1][g_current_column] = '\0';
    }

    void append_text(const char* text)
    {
        if (text == nullptr)
        {
            return;
        }

        for (size_t index = 0; text[index] != '\0'; ++index)
        {
            append_char(text[index]);
        }
    }

    void append_line(const char* text)
    {
        append_text(text);
        append_char('\n');
    }

    size_t line_count()
    {
        return g_line_count;
    }

    size_t scroll_offset()
    {
        return g_scroll_offset;
    }

    void scroll_up(size_t lines)
    {
        if (lines == 0)
        {
            return;
        }

        if (g_scroll_offset + lines > g_line_count)
        {
            g_scroll_offset = g_line_count > 0 ? g_line_count - 1 : 0;
            return;
        }

        g_scroll_offset += lines;
    }

    void scroll_down(size_t lines)
    {
        if (lines >= g_scroll_offset)
        {
            g_scroll_offset = 0;
            return;
        }

        g_scroll_offset -= lines;
    }

    void scroll_to_bottom()
    {
        g_scroll_offset = 0;
    }

    const char* line_at(size_t index)
    {
        if (index >= g_line_count)
        {
            return "";
        }

        return g_lines[index];
    }

    size_t visible_start(size_t viewport_lines)
    {
        if (g_line_count <= viewport_lines)
        {
            return 0;
        }

        const size_t max_offset = g_line_count - viewport_lines;
        return max_offset > g_scroll_offset ? max_offset - g_scroll_offset : 0;
    }

    size_t visible_count(size_t viewport_lines)
    {
        const size_t start = visible_start(viewport_lines);
        const size_t remaining = g_line_count - start;
        return remaining < viewport_lines ? remaining : viewport_lines;
    }

    bool validation_self_test()
    {
        initialize();
        append_line("test");
        return line_count() >= 1 && tinyos::ui::gfx_scrollback::MaxLines >= 64;
    }
}
