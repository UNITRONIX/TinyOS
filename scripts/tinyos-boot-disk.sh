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
cp "$ISO" "$BOOT_DISK"

echo "Boot disk image ready: $BOOT_DISK (hybrid multiboot from $ISO)"
