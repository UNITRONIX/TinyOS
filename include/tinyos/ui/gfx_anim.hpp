#pragma once

#include <stdint.h>

namespace tinyos::ui::gfx_anim
{
    void reset_session(uint64_t start_ticks);
    uint32_t intro_progress(uint64_t ticks);
    bool intro_complete();
    size_t typewriter_length(const char* text, uint64_t ticks, uint64_t chars_per_second);
    uint8_t logo_alpha(uint64_t ticks);
    uint32_t mascot_frame(uint64_t ticks);
    float cursor_opacity(uint64_t ticks);
    bool validation_self_test();
}
