#include <stddef.h>
#include <stdint.h>

#include <tinyos/ui/gfx_anim.hpp>

namespace
{
    uint64_t g_session_start_ticks = 0;
    bool g_intro_complete = false;
    constexpr uint64_t IntroRevealTicks = 90;
    constexpr uint64_t TypewriterCharsPerSecond = 24;
    constexpr uint64_t CursorBlinkTicks = 15;
}

namespace tinyos::ui::gfx_anim
{
    void reset_session(uint64_t start_ticks)
    {
        g_session_start_ticks = start_ticks;
        g_intro_complete = false;
    }

    uint32_t intro_progress(uint64_t ticks)
    {
        if (g_intro_complete)
        {
            return 100;
        }

        const uint64_t elapsed = ticks > g_session_start_ticks ? ticks - g_session_start_ticks : 0;
        if (elapsed >= IntroRevealTicks)
        {
            g_intro_complete = true;
            return 100;
        }

        return static_cast<uint32_t>((elapsed * 100) / IntroRevealTicks);
    }

    bool intro_complete()
    {
        return g_intro_complete;
    }

    size_t typewriter_length(const char* text, uint64_t ticks, uint64_t chars_per_second)
    {
        if (text == nullptr)
        {
            return 0;
        }

        const uint32_t elapsed = static_cast<uint32_t>(ticks > g_session_start_ticks ? ticks - g_session_start_ticks : 0);
        const uint32_t rate = static_cast<uint32_t>(chars_per_second == 0 ? TypewriterCharsPerSecond : chars_per_second);
        const uint32_t chars = (elapsed * rate) / 100u;
        size_t length = 0;
        while (text[length] != '\0')
        {
            ++length;
        }

        return chars > length ? length : static_cast<size_t>(chars);
    }

    uint8_t logo_alpha(uint64_t ticks)
    {
        const uint32_t progress = intro_progress(ticks);
        if (progress <= 20)
        {
            return 0;
        }

        if (progress >= 40)
        {
            return 255;
        }

        return static_cast<uint8_t>(((progress - 20) * 255) / 20);
    }

    uint32_t mascot_frame(uint64_t ticks)
    {
        return static_cast<uint32_t>((static_cast<uint32_t>(ticks) / 45u) % 2u);
    }

    float cursor_opacity(uint64_t ticks)
    {
        const uint64_t phase = (ticks / CursorBlinkTicks) % 2;
        return phase == 0 ? 1.0f : 0.15f;
    }

    bool validation_self_test()
    {
        reset_session(0);
        return intro_progress(IntroRevealTicks) == 100;
    }
}
