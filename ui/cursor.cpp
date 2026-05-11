#include <tinyos/ui/cursor.hpp>
#include <tinyos/ui/renderer.hpp>

namespace
{
    constexpr uint8_t CursorAttribute = 0x2F;
    tinyos::ui::cursor::State g_state = {};
    uint64_t g_movements = 0;
    uint64_t g_renders = 0;
    uint64_t g_rejected_operations = 0;

    uint32_t clamp_u32(uint32_t value, uint32_t max_exclusive)
    {
        if (max_exclusive == 0)
        {
            return 0;
        }

        return value < max_exclusive ? value : max_exclusive - 1;
    }

    uint32_t apply_delta(uint32_t value, int32_t delta, uint32_t max_exclusive)
    {
        if (delta < 0)
        {
            const uint32_t magnitude = static_cast<uint32_t>(-delta);
            return value > magnitude ? value - magnitude : 0;
        }

        return clamp_u32(value + static_cast<uint32_t>(delta), max_exclusive);
    }
}

namespace tinyos::ui::cursor
{
    void initialize()
    {
        const auto* renderer_state = tinyos::ui::renderer::state();
        if (renderer_state == nullptr || !renderer_state->ready)
        {
            g_state.ready = false;
            g_state.visible = false;
            g_state.column = 0;
            g_state.row = 0;
            g_state.max_columns = 0;
            g_state.max_rows = 0;
            return;
        }

        g_state.ready = true;
        g_state.visible = true;
        g_state.max_columns = renderer_state->width;
        g_state.max_rows = renderer_state->height;
        g_state.column = renderer_state->width > 1 ? renderer_state->width / 2 : 0;
        g_state.row = renderer_state->height > 1 ? renderer_state->height / 2 : 0;
        g_movements = 0;
        g_renders = 0;
        g_rejected_operations = 0;
    }

    bool is_ready()
    {
        return g_state.ready;
    }

    const State* state()
    {
        return &g_state;
    }

    bool set_visible(bool visible)
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

        g_state.visible = visible;
        return true;
    }

    bool move_to(uint32_t column, uint32_t row)
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

        g_state.column = clamp_u32(column, g_state.max_columns);
        g_state.row = clamp_u32(row, g_state.max_rows);
        ++g_movements;
        return true;
    }

    bool move_by(int32_t delta_column, int32_t delta_row)
    {
        if (!g_state.ready)
        {
            ++g_rejected_operations;
            return false;
        }

        g_state.column = apply_delta(g_state.column, delta_column, g_state.max_columns);
        g_state.row = apply_delta(g_state.row, delta_row, g_state.max_rows);
        ++g_movements;
        return true;
    }

    bool handle_event(const tinyos::ui::events::Event& event)
    {
        if (event.type == tinyos::ui::events::EventType::Pointer)
        {
            return move_to(event.column, event.row);
        }

        if (event.type == tinyos::ui::events::EventType::MouseButton)
        {
            return move_to(event.column, event.row);
        }

        ++g_rejected_operations;
        return false;
    }

    bool render()
    {
        if (!g_state.ready || !g_state.visible)
        {
            ++g_rejected_operations;
            return false;
        }

        if (!tinyos::ui::renderer::draw_text(g_state.column, g_state.row, "+", CursorAttribute))
        {
            ++g_rejected_operations;
            return false;
        }

        ++g_renders;
        return true;
    }

    uint64_t movement_count()
    {
        return g_movements;
    }

    uint64_t render_count()
    {
        return g_renders;
    }

    uint64_t rejected_operation_count()
    {
        return g_rejected_operations;
    }

    bool validation_self_test()
    {
        return g_state.ready &&
            g_state.visible &&
            g_state.max_columns != 0 &&
            g_state.max_rows != 0 &&
            g_state.column < g_state.max_columns &&
            g_state.row < g_state.max_rows;
    }

    bool render_validation_self_test()
    {
        return validation_self_test() && render();
    }
}