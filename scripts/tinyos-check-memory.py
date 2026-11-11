#!/usr/bin/env python3
"""Verify that the kernel's managed frame pool tracks the machine's RAM.

Usage:
    python3 scripts/tinyos-check-memory.py <boot-log> <qemu-memory-size>

`qemu-memory-size` accepts the same forms QEMU does, e.g. `32M`, `512M`, `2G`.

The kernel logs a line like:
    Frame pool: 3070 MiB free of 3071 MiB managed.

A regression here is not a cosmetic failure: the frame allocator used to be
clamped to 16 MiB, and later to a 256 MiB bitmap, so a machine with gigabytes of
RAM silently received a pool of a few megabytes. This check makes that class of
bug impossible to merge unnoticed.
"""
import re
import sys

# The kernel reserves low memory, the kernel image, boot modules, the memory map
# and the allocator bitmap itself. Allow a generous margin around the expected
# figure so the check catches a clamped pool without tripping on small changes to
# what the kernel reserves.
MIN_RATIO = 0.90


def parse_size(text: str) -> int:
    """Convert a QEMU memory size into bytes."""
    match = re.fullmatch(r"\s*(\d+)\s*([KMGkmg])?\s*", text)
    if not match:
        raise ValueError(f"cannot parse memory size: {text!r}")

    value = int(match.group(1))
    suffix = (match.group(2) or "").lower()
    multiplier = {"": 1, "k": 1024, "m": 1024 ** 2, "g": 1024 ** 3}[suffix]
    return value * multiplier


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    log_path, memory_text = sys.argv[1], sys.argv[2]

    try:
        requested_bytes = parse_size(memory_text)
    except ValueError as error:
        print(error)
        return 2

    try:
        with open(log_path, "r", errors="replace") as handle:
            log = handle.read()
    except OSError as error:
        print(f"cannot read {log_path}: {error}")
        return 2

    match = re.search(r"Frame pool: (\d+) MiB free of (\d+) MiB managed", log)
    if not match:
        print("no frame pool line in the boot log")
        return 1

    free_mib = int(match.group(1))
    managed_mib = int(match.group(2))
    requested_mib = requested_bytes // (1024 * 1024)

    minimum_mib = int(requested_mib * MIN_RATIO)
    status = "ok" if managed_mib >= minimum_mib else "FAIL"

    print(f"  {status:4} {memory_text:<6} managed={managed_mib} MiB "
          f"free={free_mib} MiB (expected >= {minimum_mib} MiB)")

    return 0 if status == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
