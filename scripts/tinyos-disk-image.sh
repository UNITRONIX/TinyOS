#!/usr/bin/env bash
set -euo pipefail

DISK_IMAGE="${1:-build/tinyos-disk.img}"
DISK_SECTORS="${2:-8192}"

mkdir -p "$(dirname "$DISK_IMAGE")"
dd if=/dev/zero of="$DISK_IMAGE" bs=512 count="$DISK_SECTORS" status=none

python3 - "$DISK_IMAGE" "$DISK_SECTORS" <<'PY'
import struct
import sys

disk_path = sys.argv[1]
disk_sectors = sys.argv[2]

volume_text = (
    f"TinyOS block volume\n"
    f"name=virtio-blk0\n"
    f"mode=read-write\n"
    f"sector-size=512\n"
    f"sectors={disk_sectors}\n"
).encode("ascii")

readme = b"TinyOS persistent volume\nFiles are stored on the VirtIO block device.\n"
layout = b"/system\n/users\n/apps\n/logs\n/receipts\n"
notes = b"TinyOS block notes.\n"

files = [
    (b"README.txt", 2, readme, 0),
    (b"layout.txt", 3, layout, 0),
    (b"notes.txt", 4, notes, 1),
]

def build_catalog():
    catalog = bytearray(256)
    catalog[0:4] = b"TOSF"
    catalog[4] = 1
    catalog[5] = len(files)

    entry_base = 64
    entry_stride = 64
    for index, (name, sector, payload, flags) in enumerate(files):
        offset = entry_base + index * entry_stride
        name_bytes = name[:47]
        catalog[offset:offset + len(name_bytes)] = name_bytes
        struct.pack_into("<I", catalog, offset + 48, sector)
        struct.pack_into("<I", catalog, offset + 52, len(payload))
        struct.pack_into("<I", catalog, offset + 56, flags)

    return bytes(catalog)

sector0 = bytearray(512)
copy_size = min(len(volume_text), 255)
sector0[:copy_size] = volume_text[:copy_size]
sector0[256:512] = build_catalog()

catalog_sector = bytearray(512)
catalog_sector[0:256] = build_catalog()

with open(disk_path, "r+b") as disk:
    disk.seek(0)
    disk.write(sector0)
    disk.seek(512)
    disk.write(catalog_sector)
    for _name, sector, payload, _flags in files:
        sector_data = payload + b"\x00" * (512 - len(payload))
        disk.seek(sector * 512)
        disk.write(sector_data[:512])
PY

echo "Disk image ready: $DISK_IMAGE ($DISK_SECTORS sectors)"
