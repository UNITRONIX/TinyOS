#!/usr/bin/env bash
set -euo pipefail
echo "=== TinyOS toolchain (WSL) ==="
for t in clang++ nasm ld.lld make grub-mkrescue xorriso qemu-system-i386 timeout; do
  if command -v "$t" >/dev/null 2>&1; then
    echo "OK  $t -> $(command -v "$t")"
  else
    echo "MISS $t"
  fi
done
echo
clang++ --version | head -1
ld.lld --version | head -1
nasm -v
grub-mkrescue --version 2>&1 | head -1
qemu-system-i386 --version | head -1
cd /mnt/c/Users/UNITRONIX/Documents/GitHub/TinyOS
make check-build-tools
make check-image-tools
make check-qemu-tools
