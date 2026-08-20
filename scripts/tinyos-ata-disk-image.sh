#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

OUT="${1:-build/tinyos-ata.img}"
SECTORS="${2:-8192}"

mkdir -p "$(dirname "$OUT")"
python3 - "$OUT" "$SECTORS" <<'PY'
import sys
from pathlib import Path
path = Path(sys.argv[1])
sectors = int(sys.argv[2])
path.write_bytes(b"\x00" * (sectors * 512))
print(f"Blank ATA disk image ready: {path} ({sectors} sectors)")
PY
