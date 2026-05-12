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

## Target Install Runtime

The current tested install media is still the QEMU ISO path for `i686`. The target install matrix is:

- `i686` / QEMU PC: current boot reference, installer planned.
- `x86_64` / QEMU PC: planned after the 32-bit reference path is stable.
- `aarch64` / QEMU `virt`: planned after architecture and platform contracts are stable.
- Disk install: planned after persistent block storage and filesystem support exist.
- Terminal installer: planned, documented in `docs/installed-system-pattern.md`.

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

## Lower-Memory Probing

The supported baseline remains 32 MiB until repeated boot tests prove a smaller value. Use the probe target for experiments:

```sh
make test-minimal-probe
make test-minimal-probe MINIMAL_PROBE_MEMORY="32M 24M 16M"
```

Record any lower passing value in this document only after the boot smoke markers, provisioning manifest, terminal UI and storage diagnostics remain stable.