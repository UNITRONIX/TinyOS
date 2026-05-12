# TinyOS

TinyOS is a minimal freestanding `i686` kernel written in `C++` and booted by `GRUB` via the Multiboot specification. The project is organized into small modules so it stays easy to extend and can later be ported to other architectures.

TinyOS source code and documentation are licensed under the Apache License 2.0 unless a file explicitly states otherwise. See `LICENSE` and `docs/licensing.md`.

## Features

- `GRUB` bootable kernel
- text mode `VGA` output
- polling-based `PS/2` keyboard input
- tiny text shell
- shell diagnostics and TinyOS-native RAMFS tools such as `files`, `fsmap`, `show`, `describe` and `write`
- static TinyOS device registry with `devices` diagnostics
- RAM-backed block device scaffold with `blockinfo` diagnostics
- read-only block VFS mount under `/volumes` with `storageinfo` diagnostics
- framebuffer/text-grid surface scaffold with `fbinfo` diagnostics
- renderer scaffold with `renderinfo`, `rendertest` and `renderfilltest` diagnostics
- terminal UI scaffold with `terminalinfo`, `terminaltest`, `terminalclear` and `terminalpaneltest` diagnostics
- TUI widget scaffold with `widgetinfo`, `widgettest`, `widgetdispatch` and `widgetactiontest` diagnostics
- UI event queue scaffold with `uieventinfo`, `uieventpump`, `uieventpeek` and `uieventtest` diagnostics
- language/runtime manifest with `runtimeinfo` diagnostics for native and planned sandboxed app targets
- application capability profile manifest with `appinfo` diagnostics for future native, GUI, web-style and self-hosted apps
- application launch policy dry-checks with `launchinfo` and `launchcheck`
- `.tapp` application package registry, trust store and install-gate checks with `tappinfo`, `tapps`, `tapp`, `tappcheck`, `tappverify`, `trustinfo` and `trust`
- system management tool manifest with `tools`, `toolinfo` and `tool <command>` diagnostics
- secure image/provisioning manifest with `imageinfo`, `provisioninfo` and `deployinfo` diagnostics
- project provisioning workbench plan for isolated workspaces, device variants, resource budgets and remote access
- host `.tapp` signing helpers: `keygen-app`, `trust-app`, `sign-app` and `verify-app`
- documented minimum runtime target: `i686`, Multiboot ISO, VGA text mode, PS/2 keyboard and 32 MiB RAM
- simple system API layer
- `Makefile` build and `QEMU` run targets
- editable from `Visual Studio Community` as a `Makefile` project

## Quick build

```powershell
make iso
```

## Quick run

```powershell
make run
```

The default boot path starts the TinyOS terminal. The framebuffer desktop remains an optional preview through `make run-framebuffer-preview`.

## Roadmaps

- `docs/os-roadmap.md` - staged plan for growing TinyOS into a real OS.
- `docs/implementation-roadmap.md` - current implementation milestones.
- `docs/security-roadmap.md` - security and hardening priorities.
- `docs/provisioning-workbench.md` - project provisioning workflow and tool plan.
- `docs/system-requirements.md` - minimum and recommended runtime/build requirements.
- `docs/licensing.md` - Apache-2.0 policy and third-party boundary notes.

## License

TinyOS project source is Apache-2.0 by default. The current ISO build uses GRUB as an external bootloader through `grub-mkrescue`/`grub2-mkrescue`; distributed boot images that include GRUB may have separate bootloader license obligations.

## Quick test

```powershell
make prepare-test-env
make test-boot
make test-minimal
```

The boot smoke test runs QEMU headlessly and writes serial output to `build/boot-smoke.log`.
If the build toolchain is not installed yet but `build/tinyos.iso` already exists, use `make test-existing-iso`.
For a longer runtime check, use `make test-stability`.

## Visual Studio note

The included `TinyOS.vcxproj` is a `Makefile` project for editing in `Visual Studio Community`. Use `MSYS2` and set the `MSYS2_BASH` environment variable if the helper script `scripts/build.ps1` cannot find `bash.exe` automatically from the IDE.
