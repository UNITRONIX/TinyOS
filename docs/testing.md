# TinyOS Testing Guide

TinyOS uses QEMU as the primary safety loop. The ISO path is the reference test path until raw disk image generation is implemented.

## Required tools

- `make`
- `nasm`
- `i686-elf-g++` or `clang++` with `ld.lld`
- `grub-mkrescue` or `grub2-mkrescue`
- `xorriso`
- `qemu-system-i386`
- `timeout`

On Fedora-family systems, the host packages are usually close to:

```sh
sudo dnf install clang lld nasm grub2-tools-extra xorriso qemu-system-x86
```

If an `i686-elf-g++` cross compiler is available, TinyOS will prefer it. Otherwise the Makefile uses the Clang `i686-elf` target.

## Commands

Check whether the local environment can build and test TinyOS:

```sh
make prepare-test-env
```

Build the bootable ISO:

```sh
make iso
```

Run a headless boot smoke test in QEMU:

```sh
make test-boot
```

The smoke test captures serial output through QEMU's serial file backend in `build/boot-smoke.log` and passes when QEMU keeps running until the timeout and the boot, architecture capability manifest, platform compatibility manifest, PC platform initialization contract, PC required device classes, kernel section protection contract, boot-module validation, boot-module address-space tracking, ELF validation, RAMFS file tools, syscall validation, syscall boundary policy, syscall definition table, syscall filter policy, syscall resource limit policy, device registry, RAM block device, block VFS mount, framebuffer surface, device RAMFS metadata, renderer, terminal UI, TUI widgets, window manager, desktop shell prototype, UI event queue, address-space, address-space protection flag, address-space paging policy diagnostics, bootstrap paging policy application, runtime paging, paging, paging protection flag, PIT IRQ0, keyboard IRQ1, task stack ownership, context ABI and scheduler tick markers appear.

Run a longer runtime stability check:

```sh
make test-stability
```

By default this uses a 20 second QEMU window. Override it with `STABILITY_TEST_TIMEOUT=60s` for a longer run.

Run the current minimum runtime envelope test:

```sh
make test-minimal
```

By default this boots TinyOS with `MINIMAL_TEST_MEMORY=32M` and checks the boot, requirements, renderer, terminal UI, UI event queue and storage mount markers. Override it with `MINIMAL_TEST_MEMORY=64M` when comparing low-memory behavior.

If the build toolchain is not installed yet but `build/tinyos.iso` already exists, run the existing artifact through QEMU:

```sh
make test-existing-iso
```

Run interactively with VGA output:

```sh
make run
```

Run the debug boot image:

```sh
make debug-run
```

## Current limitation

The project currently builds an ISO image. A USB or microSD-friendly raw `.img` workflow is planned after the boot media design and storage assumptions are made explicit.