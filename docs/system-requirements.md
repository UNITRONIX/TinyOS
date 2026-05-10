# TinyOS System Requirements

This document records the current tested runtime envelope. It is intentionally small and should evolve only when boot tests prove the new baseline.

## Current Minimum Runtime

- CPU: `i686` compatible 32-bit processor.
- Boot path: GRUB Multiboot ISO on a BIOS-style PC platform.
- RAM: 32 MiB tested by `make test-minimal`.
- Display: VGA text mode, 80x25 cells.
- Input: PS/2 keyboard controller.
- Timer: 8253/8254 PIT configured at 100 Hz.
- Interrupt controller: 8259 PIC.
- Storage: ISO boot media plus RAM-backed block storage scaffold.
- Emulator: `qemu-system-i386`.

## Recommended Development Runtime

- RAM: 128 MiB or more for future paging, process and GUI experiments.
- Serial output enabled for diagnostics during kernel work.
- Headless QEMU smoke tests before interactive testing.

## Host Build Requirements

- `make`
- `clang++` with `ld.lld` or an `i686-elf-g++` cross compiler
- `nasm`
- `grub-mkrescue` or `grub2-mkrescue`
- `xorriso`
- `qemu-system-i386`
- `timeout`

## Verification

Use these commands as the current quality gate:

```sh
make prepare-test-env
make test-boot
make test-minimal
make test-stability
```