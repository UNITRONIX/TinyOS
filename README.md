# TinyOS

TinyOS is a minimal freestanding `i686` kernel written in `C++` and booted by `GRUB` via the Multiboot specification. The project is organized into small modules so it stays easy to extend and can later be ported to other architectures.

TinyOS is authored by UNITRONIX (Krzysztof Nienartowicz). TinyOS source code and documentation are licensed under the Apache License 2.0 unless a file explicitly states otherwise. See `LICENSE`, `NOTICE` and `docs/licensing.md`.

## AI-Assisted Development

TinyOS is developed as an AI-assisted operating system project. AI assistance is used for planning, documentation, implementation slices, code review and test planning, while the repository keeps the project grounded in explicit source files, roadmaps and reproducible build/test commands.

AI assistance is not a runtime dependency. The current TinyOS kernel does not include an AI subsystem; the AI-assisted part describes the development workflow used to grow the system gradually and safely.

## Current Project Knowledge

- The current reference target is `i686` on QEMU PC hardware, booted as a `GRUB` Multiboot ISO.
- The documented minimum runtime envelope is still 32 MiB RAM; lower-memory runs are experimental probes before any baseline change. The desktop-capable ISO has reached `2561K` on QEMU/GRUB. The physical terminal-only build profile currently reaches `2529K`, while `2528K` and `64K` do not reach TinyOS serial output.
- TinyOS is terminal-first today, with VGA text output, PS/2 keyboard input, `sysinfo`, `status`/`syscheck` diagnostics, risk listing, active profile checks, path inspection, `filemgr`, `textedit`, `fileui` and RAMFS metadata. A `TERMINAL_ONLY=1` build profile can omit the desktop, window manager, cursor and PS/2 mouse paths from the linked kernel.
- The GUI path is planned incrementally through renderer, terminal, widget and event scaffolds before a richer desktop becomes the default.
- Security work is treated as a high-priority track: trusted package metadata, image/provisioning contracts, integrity diagnostics and password-hashing policy are documented before broader runtime privileges are enabled.
- Project provisioning is host-first for now, with signed/encrypted artifacts, isolated project workspaces, device variants and remote access kept behind explicit opt-in policy.
- Installed-system support is currently a ready contract plus a RAMFS mock: `examples/install.profile`, `install-plan`, `check-install-profile`, `/system/install.txt`, `/system/profile.txt`, `installinfo`, `installcheck`, `install`, `profileinfo`, `profilecheck` and `/receipts/install.receipt` exist, while real disk installation is planned after persistent storage and filesystem contracts mature.
- Future portability targets include `x86_64` and `aarch64`, but they should only be promoted after repeatable boot, storage and smoke tests exist for each target.

## Features

- `GRUB` bootable kernel
- text mode `VGA` output
- polling-based `PS/2` keyboard input
- tiny text shell
- shell diagnostics and TinyOS-native RAMFS tools such as `sysinfo`, `status`, `syscheck`, `riskinfo`, `profileinfo`, `profilecheck`, `helpsearch`, `pathcheck`, `filemgr`, `textedit`, `fileui`, `files`, `fsmap`, `show`, `describe`, `write` and `edit`
- terminal file workflow: `filemgr` provides a two-pane RAMFS manager with view, edit, create, delete, copy and move actions; `textedit` provides an interactive RAMFS editor with load, replace, append, clear, save, reload and file-info actions; `fileui` remains as a lighter single-pane browser
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
- installed-system contract with `installinfo`, `installcheck`, RAMFS `install` mock, active RAMFS profile checks and host install-profile validation
- project provisioning workbench plan for isolated workspaces, device variants, resource budgets and remote access
- host `.tapp` signing helpers: `keygen-app`, `trust-app`, `sign-app` and `verify-app`
- documented minimum runtime target: `i686`, Multiboot ISO, VGA text mode, PS/2 keyboard and 32 MiB RAM, with experimental terminal-only probes down to `2529K`
- simple system API layer
- `Makefile` build and `QEMU` run targets
- editable from `Visual Studio Community` as a `Makefile` project

## Quick build

```powershell
make iso
```

Build the experimental terminal-only ISO without the desktop/window-manager/mouse objects:

```powershell
make terminal-only-iso
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
- `docs/installed-system-pattern.md` - target install flow and installed-system documentation pattern.
- `docs/provisioning-workbench.md` - project provisioning workflow and tool plan.
- `docs/system-requirements.md` - minimum and recommended runtime/build requirements.
- `docs/licensing.md` - Apache-2.0 policy and third-party boundary notes.

## License

TinyOS project source is Apache-2.0 by default. Project ownership and author metadata are recorded in `NOTICE`. The current ISO build uses GRUB as an external bootloader through `grub-mkrescue`/`grub2-mkrescue`; distributed boot images that include GRUB may have separate bootloader license obligations.

## Quick test

```powershell
make prepare-test-env
make test-boot
make test-minimal
```

The boot smoke test runs QEMU headlessly and writes serial output to `build/boot-smoke.log`.
If the build toolchain is not installed yet but `build/tinyos.iso` already exists, use `make test-existing-iso`.
For a longer runtime check, use `make test-stability`.
For experimental low-memory measurements, use `make test-lowmem-probe` for the default ISO or `make test-terminal-lowmem-probe` for the physical terminal-only profile.

## Visual Studio note

The included `TinyOS.vcxproj` is a `Makefile` project for editing in `Visual Studio Community`. Use `MSYS2` and set the `MSYS2_BASH` environment variable if the helper script `scripts/build.ps1` cannot find `bash.exe` automatically from the IDE.
