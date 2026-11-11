#!/usr/bin/env bash
# Remove every desktop/GUI/pointer source file and rewrite the build list.
#
# The desktop stack (window manager, desktop shell, framebuffer terminal,
# cursor, PS/2 mouse and the gfx_* helpers that only served them) does not
# implement a working product surface and is not part of the OS bring-up plan.
# Keeping it linked into the kernel costs boot memory and hides real work behind
# "scaffold ready" log lines.
#
# Usage: bash scripts/tinyos-drop-desktop.sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

files=(
    drivers/mouse.cpp
    include/tinyos/drivers/mouse.hpp
    ui/cursor.cpp
    include/tinyos/ui/cursor.hpp
    ui/desktop.cpp
    include/tinyos/ui/desktop.hpp
    ui/window_manager.cpp
    include/tinyos/ui/window_manager.hpp
    ui/graphical_desktop.cpp
    include/tinyos/ui/graphical_desktop.hpp
    ui/gfx_terminal.cpp
    include/tinyos/ui/gfx_terminal.hpp
    ui/gfx_input.cpp
    include/tinyos/ui/gfx_input.hpp
    ui/gfx_console.cpp
    include/tinyos/ui/gfx_console.hpp
    ui/gfx_theme.cpp
    include/tinyos/ui/gfx_theme.hpp
    ui/gfx_anim.cpp
    include/tinyos/ui/gfx_anim.hpp
    ui/gfx_picker.cpp
    include/tinyos/ui/gfx_picker.hpp
    ui/font_logo.cpp
    include/tinyos/ui/font_logo.hpp
    docs/gfx-terminal-roadmap.md
    docs/gfx-terminal-user.md
)

for file in "${files[@]}"; do
    if [ -e "$file" ]; then
        git rm -q --cached "$file" 2>/dev/null || true
        rm -f "$file"
        echo "removed $file"
    fi
done

echo "desktop stack removed"
