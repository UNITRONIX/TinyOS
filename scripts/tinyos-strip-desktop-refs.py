#!/usr/bin/env python3
"""Strip the desktop/GUI stack out of kernel.cpp and shell.cpp.

The desktop sources are deleted outright by scripts/tinyos-drop-desktop.sh.
Two files still reference them inside TINYOS_TERMINAL_ONLY guards; this script
rewrites those files so the build no longer depends on any deleted header, log
marker or shell command.

Implementation note: guarded regions are removed with a line-based scanner
rather than a regular expression. Nested `#if` blocks inside a large translation
unit make backtracking regexes quadratic-to-exponential, which used to hang this
step.

The script is idempotent: running it twice leaves the files unchanged.

Usage:
    python3 scripts/tinyos-strip-desktop-refs.py [--check]
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Guards whose entire body disappears, including nested #if/#else branches.
DROP_GUARDS = (
    "!defined(TINYOS_TERMINAL_ONLY)",
    "defined(TINYOS_GRAPHICAL_BOOT)",
    "defined(TINYOS_GRAPHICAL_AUTOSTART)",
    "defined(TINYOS_GFX_TERM_AUTOSTART)",
)


def drop_guarded_blocks(lines):
    """Remove `#if <drop guard>` ... matching `#endif` regions.

    Returns the surviving lines. Nested conditionals are tracked so the wrong
    `#endif` is never matched.
    """
    result = []
    skip_depth = 0

    for line in lines:
        stripped = line.strip()

        if skip_depth == 0 and stripped.startswith("#if "):
            guard = stripped[4:].strip()
            if guard in DROP_GUARDS:
                skip_depth = 1
                continue
            result.append(line)
            continue

        if skip_depth > 0:
            if stripped.startswith("#if"):
                skip_depth += 1
                continue
            if stripped.startswith("#endif"):
                skip_depth -= 1
                continue
            # Everything else inside the dropped region, including #else and
            # #elif branches, is discarded.
            continue

        result.append(line)

    return result


def read_lines(path):
    return path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")


def write_lines(path, lines):
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def rewrite_kernel_cpp(lines):
    text = "\n".join(lines)

    # Keep the terminal-only boot marker that the smoke tests assert on; the
    # desktop markers beside it are dropped with the guard.
    text = text.replace(
        """#if !defined(TINYOS_TERMINAL_ONLY)
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Cursor scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Window manager scaffold ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Desktop shell prototype ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Fullscreen desktop mode ready.");
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Desktop input interactions ready.");
#else
    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Terminal-only low-memory profile ready.");
#endif""",
        """    tinyos::kernel::klog::write_line(tinyos::kernel::klog::Level::Info, "Terminal-only low-memory profile ready.");""",
    )

    lines = text.split("\n")
    lines = drop_guarded_blocks(lines)
    return lines


def rewrite_shell_cpp(lines):
    text = "\n".join(lines)

    # Build profile reporting: the desktop build no longer exists.
    text = text.replace(
        """    #if defined(TINYOS_TERMINAL_ONLY)
        tinyos::drivers::vga::write_line("terminal-only low-memory");
    #else
        tinyos::drivers::vga::write_line("desktop-capable terminal-first");
    #endif""",
        """        tinyos::drivers::vga::write_line("terminal-only");""",
    )

    lines = drop_guarded_blocks(text.split("\n"))

    # The two commands that used to be framebuffer-only stay, but honest about
    # what this build can do.
    text = "\n".join(lines)
    replacement = """        if (core::string::compare(command, "terminaltheme") == 0 || core::string::starts_with(command, "terminaltheme "))
        {
            tinyos::drivers::vga::write_line("Terminal style: monochrome VGA text.");
            tinyos::drivers::vga::write_line("Theme presets need a framebuffer terminal, which is not part of this build.");
            return;
        }

        if (core::string::compare(command, "videomode") == 0 || core::string::starts_with(command, "videomode "))
        {
            tinyos::drivers::vga::write_line("Display: VGA text mode, 80x25.");
            tinyos::drivers::vga::write_line("Linear framebuffer: absent in this build.");
            return;
        }

"""
    anchor = '        if (core::string::compare(command, "widgetdispatch") == 0)\n'
    if "Terminal style: monochrome VGA text." not in text and anchor in text:
        text = text.replace(anchor, replacement + anchor, 1)

    return text.split("\n")


def main() -> int:
    check_only = "--check" in sys.argv
    targets = (
        (ROOT / "kernel/kernel.cpp", rewrite_kernel_cpp),
        (ROOT / "shell/shell.cpp", rewrite_shell_cpp),
    )

    changed = []
    for path, rewrite in targets:
        original = read_lines(path)
        updated = rewrite(original)
        if updated != original:
            changed.append(path.name)
            if not check_only:
                write_lines(path, updated)

    if changed:
        print("rewritten: " + ", ".join(changed))
        return 1 if check_only else 0

    print("no desktop references left")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
