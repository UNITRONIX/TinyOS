#!/usr/bin/env python3
"""Resolve a TinyOS kernel address to nearby symbols.

Usage:
    python3 scripts/tinyos-resolve.py <symbols.txt> <hex-address>

`build/symbols.txt` is produced with:
    nm -C -n <kernel-image> > build/symbols.txt
"""
import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    symbols_path, address_text = sys.argv[1], sys.argv[2]
    target = int(address_text, 16)

    rows = []
    with open(symbols_path, "r", errors="replace") as handle:
        for line in handle:
            parts = line.split(None, 2)
            if len(parts) >= 2 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
                rows.append((int(parts[0], 16), parts[2].strip() if len(parts) > 2 else parts[1]))
    rows.sort()

    before = [row for row in rows if row[0] <= target]
    after = [row for row in rows if row[0] > target]

    if before:
        address, name = before[-1]
        print(f"  {address:#010x} +{target - address:#x}  {name}")
    print(f"? {target:#010x}  fault")
    if after:
        address, name = after[0]
        print(f"  {address:#010x}          {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
