# TinyOS System Requirements

This document records the current tested runtime envelope. It is intentionally small and should evolve only when boot tests prove the new baseline.

## Current Minimum Runtime

- CPU: `i686` compatible 32-bit processor.
- Boot path: GRUB Multiboot ISO on a BIOS-style PC platform.
- RAM: 32 MiB tested by `make test-minimal`. The frame allocator manages the full detected pool: `make test-memory-scaling` boots at 32M, 512M and 2G and requires the managed pool to track the machine's RAM. Verified manually up to 3 GiB (3071 MiB managed).
- Experimental low-memory RAM probe: the current ISO reaches `2561K` and fails at `2560K` and `64K` with no TinyOS serial output. The kernel links at `1M`, so a `64K` machine cannot run this image regardless of the memory allocator.
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
- `x86_64` / QEMU PC: long-mode bring-up path (`make ARCH=x86_64 iso`, `make test-boot-x86_64`). Runs the boot contract, not the kernel.
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
make test-boot-x86_64
make test-memory-scaling
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

For very small values, use the core-marker low-memory probe:

```sh
make test-lowmem-probe
make test-lowmem-probe LOWMEM_PROBE_MEMORY="4M 3M 2880K 2624K 2561K 2560K 2M 64K"
```

Current measured result on the reference QEMU/GRUB ISO path:

- `2561K` passes core boot (system requirements, terminal UI) with all boot markers present; `2560K` fails before TinyOS emits serial output.
- `64K`: fails before TinyOS emits serial output.

This result is not a supported baseline yet. The kernel is linked at `1M`, so a `64K` machine cannot run the current Multiboot image without a different boot/link/load architecture. Nearby KiB values can also be non-monotonic under QEMU/GRUB because the failure occurs before TinyOS serial logging begins.