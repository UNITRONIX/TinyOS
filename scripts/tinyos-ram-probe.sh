#!/usr/bin/env bash
# Boot TinyOS with several RAM sizes and report the frame pool the kernel builds.
#
# Usage: scripts/tinyos-ram-probe.sh [iso] [memory...]
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

iso="${1:-build/tinyos.iso}"
shift || true
sizes="${*:-32M 128M 512M 1G 2G 3G}"

if [ ! -f "$iso" ]; then
    echo "missing $iso; run 'make iso' first" >&2
    exit 1
fi

printf '%-8s %-34s %-7s %-6s\n' RAM FRAME_POOL BOOTED PANIC

for memory in $sizes; do
    log="build/ram-probe-${memory}.log"
    rm -f "$log"

    set +e
    timeout 10s qemu-system-i386 \
        -m "$memory" \
        -cdrom "$iso" \
        -display none \
        -serial "file:$log" \
        -no-reboot \
        -no-shutdown >/dev/null 2>&1
    set -e

    pool=$(awk -F'Frame pool: ' '/Frame pool: / { print $2; exit }' "$log" 2>/dev/null || true)
    booted=$(awk '/booted successfully/ { c++ } END { print c + 0 }' "$log" 2>/dev/null || echo 0)
    panic=$(awk '/KERNEL PANIC/ { c++ } END { print c + 0 }' "$log" 2>/dev/null || echo 0)

    printf '%-8s %-34s %-7s %-6s\n' "$memory" "${pool:-none}" "$booted" "$panic"
done
