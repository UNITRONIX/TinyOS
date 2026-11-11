#!/usr/bin/env bash
# Build a debug-boot ISO and boot it headlessly, printing the serial tail.
#
# Usage: scripts/tinyos-debug-boot.sh [seconds] [tail-lines]
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

seconds="${1:-12}"
tail_lines="${2:-30}"

make DEBUG_BOOT=1 \
    TARGET=tinyos-debug.kernel \
    ISO=build/tinyos-debug.iso \
    ISO_DIR=build/isodir-debug \
    OBJ_DIR=build/obj-debug \
    iso >build/debug-build.log 2>&1 || {
        echo "build failed:"
        grep -E "error:" build/debug-build.log | head -20
        exit 1
    }

rm -f build/debug-boot.log
set +e
timeout "${seconds}s" qemu-system-i386 \
    -cdrom build/tinyos-debug.iso \
    -display none \
    -serial file:build/debug-boot.log \
    -no-reboot \
    -no-shutdown >/dev/null 2>&1
status=$?
set -e

echo "qemu exit: ${status}"
echo "log lines: $(wc -l < build/debug-boot.log)"
echo "--- tail ---"
tail -n "$tail_lines" build/debug-boot.log
