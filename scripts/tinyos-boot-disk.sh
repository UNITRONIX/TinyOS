#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

BOOT_DISK="${1:-build/tinyos.img}"
ISO="${2:-build/tinyos.iso}"

if [[ ! -f "$ISO" ]]; then
    echo "Missing boot ISO: $ISO (run 'make iso' first)." >&2
    exit 1
fi

mkdir -p "$(dirname "$BOOT_DISK")"

# Prefer a real El Torito HDD hybrid when xorriso is available so the image
# can be written to USB with dd and booted on BIOS machines.
if command -v xorriso >/dev/null 2>&1; then
    rm -f "$BOOT_DISK"
    if xorriso -indev "$ISO" -outdev "$BOOT_DISK" -boot_image any keep -boot_image any partition_hd_emu=on >/dev/null 2>&1; then
        echo "Boot disk image ready (xorriso hybrid): $BOOT_DISK"
        exit 0
    fi
fi

# Fallback: raw ISO copy (works in QEMU -boot c with IDE).
cp "$ISO" "$BOOT_DISK"
echo "Boot disk image ready (ISO copy fallback): $BOOT_DISK"
echo "Tip: install xorriso for USB-hybrid partition emulation."
