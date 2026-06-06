#include <stddef.h>
#include <stdint.h>

#include <tinyos/core/memory.hpp>
#include <tinyos/core/string.hpp>
#include <tinyos/drivers/keyboard.hpp>
#include <tinyos/shell/completion.hpp>
#include <tinyos/ui/gfx_input.hpp>

namespace
{
    void copy_line(char* destination, size_t destination_size, const char* source)
    {
        (void)tinyos::core::memory::string_copy_safe(destination, destination_size, source != nullptr ? source : "");
    }

    void push_history(tinyos::ui::gfx_input::State* state)
    {
        if (state == nullptr || state->length == 0)
        {
            return;
        }

        if (state->history_count > 0 &&
            tinyos::core::string::compare(state->history[0], state->buffer) == 0)
        {
            return;
        }

        if (state->history_count >= tinyos::ui::gfx_input::HistorySize)
        {
            for (size_t index = tinyos::ui::gfx_input::HistorySize - 1; index > 0; --index)
            {
                copy_line(state->history[index], tinyos::ui::gfx_input::MaxLineLength + 1, state->history[index - 1]);
            }
        }
        else
        {
            for (size_t index = state->history_count; index > 0; --index)
            {
                copy_line(state->history[index], tinyos::ui::gfx_input::MaxLineLength + 1, state->history[index - 1]);
            }

            ++state->history_count;
        }

        copy_line(state->history[0], tinyos::ui::gfx_input::MaxLineLength + 1, state->buffer);
        state->history_index = 0;
    }

    void load_history(tinyos::ui::gfx_input::State* state, size_t index)
    {
        if (state == nullptr || index >= state->history_count)
        {
            return;
        }

        copy_line(state->buffer, tinyos::ui::gfx_input::MaxLineLength + 1, state->history[index]);
        state->length = tinyos::core::string::length(state->buffer);
        state->cursor = state->length;
        state->history_index = index;
    }

    void insert_char(tinyos::ui::gfx_input::State* state, char character)
    {
        if (state == nullptr || state->length + 1 >= tinyos::ui::gfx_input::MaxLineLength)
        {
            return;
        }

        if (state->cursor < state->length)
        {
            for (size_t index = state->length; index > state->cursor; --index)
            {
                state->buffer[index] = state->buffer[index - 1];
            }
        }

        state->buffer[state->cursor] = character;
        ++state->cursor;
        ++state->length;
        state->buffer[state->length] = '\0';
    }

    void delete_char(tinyos::ui::gfx_input::State* state)
    {
        if (state == nullptr || state->cursor >= state->length)
        {
            return;
        }

        for (size_t index = state->cursor; index < state->length; ++index)
        {
            state->buffer[index] = state->buffer[index + 1];
        }

        --state->length;
        state->buffer[state->length] = '\0';
    }

    void backspace(tinyos::ui::gfx_input::State* state)
    {
        if (state == nullptr || state->cursor == 0)
        {
            return;
        }

        --state->cursor;
        delete_char(state);
    }
}

namespace tinyos::ui::gfx_input
{
    void initialize(State* state)
    {
        if (state == nullptr)
        {
            return;
        }

        reset(state);
        state->history_count = 0;
        state->history_index = 0;
        for (size_t index = 0; index < tinyos::ui::gfx_input::HistorySize; ++index)
        {
            tinyos::core::memory::set(state->history[index], 0, tinyos::ui::gfx_input::MaxLineLength + 1);
        }
    }

    void reset(State* state)
    {
        if (state == nullptr)
        {
            return;
        }

        state->length = 0;
        state->cursor = 0;
        state->buffer[0] = '\0';
    }

    bool handle_key(State* state, char key)
    {
        if (state == nullptr)
        {
            return false;
        }

        if (key == '\n')
        {
            push_history(state);
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyUp)
        {
            if (state->history_count == 0)
            {
                return true;
            }

            const size_t next = state->history_index + 1;
            if (next < state->history_count)
            {
                load_history(state, next);
            }

            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyDown)
        {
            if (state->history_count == 0)
            {
                return true;
            }

            if (state->history_index == 0)
            {
                reset(state);
                return true;
            }

            load_history(state, state->history_index - 1);
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyLeft)
        {
            if (state->cursor > 0)
            {
                --state->cursor;
            }

            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyRight)
        {
            if (state->cursor < state->length)
            {
                ++state->cursor;
            }

            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyHome)
        {
            state->cursor = 0;
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyEnd)
        {
            state->cursor = state->length;
            return true;
        }

        if (key == tinyos::drivers::keyboard::KeyDelete)
        {
            delete_char(state);
            return true;
        }

        if (key == '\b')
        {
            backspace(state);
            return true;
        }

        if (key == '\t' || key == tinyos::drivers::keyboard::KeyShiftTab)
        {
            return complete_tab(state);
        }

        if (key >= 32 && key <= 126)
        {
            insert_char(state, key);
            return true;
        }

        return false;
    }

    bool complete_tab(State* state)
    {
        if (state == nullptr)
        {
            return false;
        }

        char completed[tinyos::ui::gfx_input::MaxLineLength + 1];
        if (!tinyos::shell::completion::complete_prefix(state->buffer, completed, sizeof(completed)))
        {
            return true;
        }

        copy_line(state->buffer, tinyos::ui::gfx_input::MaxLineLength + 1, completed);
        state->length = tinyos::core::string::length(state->buffer);
        state->cursor = state->length;
        return true;
    }

    const char* current_line(const State* state)
    {
        return state != nullptr ? state->buffer : "";
    }

    bool validation_self_test()
    {
        tinyos::ui::gfx_input::State state;
        initialize(&state);
        (void)handle_key(&state, 'l');
        (void)handle_key(&state, 's');
        return state.length == 2 && state.buffer[0] == 'l' && state.buffer[1] == 's';
    }
}
