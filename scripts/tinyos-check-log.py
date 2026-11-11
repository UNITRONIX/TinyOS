#!/usr/bin/env python3
"""Check a TinyOS serial boot log for required markers.

Usage:
    python3 scripts/tinyos-check-log.py <log-file> <marker-file>

The marker file contains one required substring per line. Reading markers from
a file keeps this check independent of shell quoting on any host.

Exits non-zero and prints the tail of the log when a marker is missing.
"""
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    log_path, marker_path = sys.argv[1], sys.argv[2]

    try:
        with open(log_path, "r", errors="replace") as handle:
            data = handle.read()
    except OSError as error:
        print(f"cannot read {log_path}: {error}")
        return 2

    try:
        with open(marker_path, "r", errors="replace") as handle:
            markers = [line.rstrip("\r\n") for line in handle if line.strip()]
    except OSError as error:
        print(f"cannot read {marker_path}: {error}")
        return 2

    missing = []
    for marker in markers:
        if marker in data:
            print(f"  ok    {marker}")
        else:
            print(f"  MISS  {marker}")
            missing.append(marker)

    print(f"log lines: {len(data.splitlines())}")

    if missing:
        print("--- last 20 log lines ---")
        print("\n".join(data.splitlines()[-20:]))
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
