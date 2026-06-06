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
profile = (
    b"tinyos.profile.version=0\n"
    b"state=persistent-default\n"
    b"source=/system/profile.txt\n"
    b"device.name=tinyos-dev-vm\n"
    b"device.variant=qemu-i686-terminal\n"
    b"boot.target=i686-pc-qemu\n"
    b"network.mode=disabled\n"
    b"user.bootstrap=developer\n"
    b"credential.bootstrap=prompt\n"
    b"admin.mode=same-bootstrap-secret\n"
    b"security.password_hashing=required\n"
    b"security.plaintext_secrets=forbidden\n"
    b"provisioning.encryption=required\n"
    b"provisioning.remote_access=disabled\n"
    b"storage.persistence=virtio-block\n"
    b"install.state=ready-contract\n"
)
install = (
    b"state=ready-contract\n"
    b"media=disk-image\n"
    b"profile=/system/profile.txt\n"
    b"layout=/system,/users,/apps\n"
    b"storage=persistent-block-catalog\n"
)
notes = b"TinyOS persistent user notes.\n"
example_tapp = (
    b"tinyos.tapp.version=0\n"
    b"tinyos.tapp.kind=manifest-envelope\n"
    b"app.name=example-system-tool\n"
    b"app.runtime=native-cpp-elf32\n"
    b"app.entry=/apps/example-system-tool.elf\n"
    b"state=valid-manifest\n"
)

files = [
    (b"README.txt", 2, readme, 0),
    (b"layout.txt", 3, layout, 0),
    (b"system/profile.txt", 5, profile, 0),
    (b"system/install.txt", 6, install, 0),
    (b"users/notes.txt", 4, notes, 1),
    (b"apps/example-system-tool.tapp", 7, example_tapp, 0),
]

def build_catalog():
    catalog = bytearray(512)
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

catalog_sector = bytearray(512)
catalog_sector[:] = build_catalog()

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
