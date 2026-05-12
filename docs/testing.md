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

The smoke test captures serial output through QEMU's serial file backend in `build/boot-smoke.log` and passes when QEMU keeps running until the timeout and the boot, architecture capability manifest, platform compatibility manifest, PC platform initialization contract, PC required device classes, kernel section protection contract, boot-module validation, boot-module address-space tracking, ELF validation, RAMFS file tools, syscall validation, syscall boundary policy, syscall definition table, syscall filter policy, syscall resource limit policy, device registry, RAM block device, block VFS mount, framebuffer surface, linear framebuffer boot contract, device RAMFS metadata, renderer, pixel renderer contract, cursor scaffold, terminal UI, TUI widgets, window manager, desktop shell prototype, fullscreen desktop mode, desktop input interactions, UI event queue, address-space, address-space protection flag, address-space paging policy diagnostics, bootstrap paging policy application, runtime paging, paging, paging protection flag, PIT IRQ0, keyboard IRQ1, task stack ownership, context ABI and scheduler tick markers appear.

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

Probe a small RAM range before lowering the documented baseline:

```sh
make test-minimal-probe
make test-minimal-probe MINIMAL_PROBE_MEMORY="32M 24M 16M"
```

This target runs `test-minimal` once per memory value and writes separate logs such as `build/boot-minimal-24M.log`.

Probe sub-16 MiB and KiB-scale RAM values with terminal-only boot markers:

```sh
make test-lowmem-probe
make test-lowmem-probe LOWMEM_PROBE_MEMORY="3M 2624K 2561K 2560K 2M 64K"
```

This target checks only the core boot, system requirements and terminal UI markers. It intentionally does not require desktop markers. The current desktop-capable QEMU/GRUB ISO probe passes at `2561K` and fails at `2560K` and `64K` with no TinyOS serial output.

Build and test the physical terminal-only profile, which omits the desktop, window manager, cursor and PS/2 mouse objects from the linked kernel:

```sh
make terminal-only-iso
make test-terminal-lowmem-probe
make test-terminal-lowmem-probe LOWMEM_PROBE_MEMORY="2880K 2624K 2529K 2528K 2M 64K"
```

The current terminal-only kernel is about `238K` on disk versus about `280K` for the desktop-capable kernel. The reference QEMU/GRUB probe reaches `2529K`; `2528K`, `2M` and `64K` fail before TinyOS emits serial output. Some nearby values can be non-monotonic on the GRUB/QEMU ISO path, so treat this as a boot-path probe rather than a supported RAM baseline.

Validate install-profile safety rules without writing disks:

```sh
make install-profile-check
```

Inside TinyOS, the installer mock can be checked manually without writing disks:

```text
installcheck
install
show /receipts/install.receipt
profileinfo
profilecheck
show /system/profile.txt
```

Inside the terminal, use the non-destructive operational checks before and after manual feature testing:

```text
sysinfo
status
syscheck
riskinfo
profileinfo
profilecheck
pathcheck /system/tools.txt
pathcheck /system/profile.txt
helpsearch install
helpsearch textedit
helpsearch filemgr
fileui
filemgr
textedit /users/notes.txt
```

If the build toolchain is not installed yet but `build/tinyos.iso` already exists, run the existing artifact through QEMU:

```sh
make test-existing-iso
```

Run interactively with VGA output:

```sh
make run
```

Run TinyOS and start the optional desktop from the terminal:

```sh
make run-gui
```

This uses the safe text console boot path. Start the desktop with `desktop`; use `q` to return to the terminal.

The framebuffer desktop preview is kept separate because TinyOS does not yet have a full framebuffer text console:

```sh
make run-framebuffer-preview
```

That target boots the linear framebuffer build and autostarts the graphical desktop directly. It is the current path for the pixel renderer, PS/2 mouse cursor, dock, draggable windows and clickable app launchers. Use `q` to leave the graphical session; the normal `make run-gui` path remains the safe terminal-first boot.

Run the graphical boot smoke test:

```sh
make test-gui-boot
```

Run the debug boot image:

```sh
make debug-run
```

## Current limitation

The project currently builds an ISO image. A USB or microSD-friendly raw `.img` workflow is planned after the boot media design and storage assumptions are made explicit.