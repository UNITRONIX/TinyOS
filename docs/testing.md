# TinyOS Testing Guide

TinyOS uses QEMU as the primary safety loop. The ISO path is the reference test path until raw disk image generation is implemented.

## Change scope gate (mandatory)

**Every completed change scope** (feature wave, bugfix batch, refactor touching kernel/shell/security) **must end with solid stability and security verification** before the work is considered done.

This applies to human developers and automated agents alike: do not mark a scope complete after `make test-boot` alone.

### Automated gate (required)

Run the full gate locally:

```sh
scripts/tinyos-dev.sh test-gate
# or: make test-gate
```

The gate runs, in order:

| Step | Target | Purpose |
|------|--------|---------|
| 1 | `prepare-test-env` | Toolchain present |
| 2 | `test-stability` | Boot smoke + longer QEMU window (`STABILITY_TEST_TIMEOUT`, default 20s) |
| 3 | `test-terminal-boot` | Terminal-only profile still boots |
| 4 | `install-profile-check` | Install profile contract (non-destructive) |
| 5 | `tapp-trust-test` | Package trust / verification scaffold |

Add wave-specific targets when the change touches those areas:

| Change area | Extra automated tests |
|-------------|----------------------|
| Memory / paging / low RAM | `make test-minimal`, optionally `make test-minimal-probe` |
| Graphical desktop | `make test-gui-boot` |
| Image / provisioning | `make image-deploy-check-test`, `make tapp-sign-test` |
| Scheduler / preemption | grep boot log for context-switch and watchdog markers (see Fala 1 in `docs/plan-dojrzalosci-systemu.md`) |

### Manual security checks (required in shell)

After the automated gate passes, boot TinyOS (`make run` or `scripts/tinyos-dev.sh run-serial`) and run non-destructive diagnostics:

```text
sysinfo
status
syscheck
riskinfo
profileinfo
profilecheck
securityinfo
integritycheck
```

All checks above must report healthy / ok for the changed subsystem. Record failures in the commit message or PR test plan.

### Definition of done

A change scope is **done** only when:

1. `make test-gate` exits 0
2. Manual `syscheck`, `securityinfo` and `integritycheck` show no regressions
3. Wave-specific acceptance criteria from `docs/plan-dojrzalosci-systemu.md` are met
4. New boot markers or shell commands introduced by the change are covered by `make test-boot` or documented in this file

---

- `make`
- `nasm`
- `i686-elf-g++` or `clang++` with `ld.lld`
- `grub-mkrescue` or `grub2-mkrescue`
- `xorriso`
- `qemu-system-i386`
- `timeout`

On Fedora 44, install the missing build tools with:

```sh
scripts/tinyos-dev.sh install-deps
sudo dnf install -y clang lld nasm make grub2-tools-extra xorriso qemu-system-x86
```

Or in one step:

```sh
scripts/tinyos-dev.sh install-deps --install
```

If an `i686-elf-g++` cross compiler is available, TinyOS will prefer it. Otherwise the Makefile uses `clang++` with the `i686-elf` target and `lld` as linker.

## Commands

Local development helper (recommended):

```sh
scripts/tinyos-dev.sh help
scripts/tinyos-dev.sh check
scripts/tinyos-dev.sh iso
scripts/tinyos-dev.sh test
scripts/tinyos-dev.sh run-serial
```

Install host dependencies on Fedora:

```sh
scripts/tinyos-dev.sh install-deps --install
```

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

The smoke test captures serial output through QEMU's serial file backend in `build/boot-smoke.log` and passes when QEMU keeps running until the timeout and the boot, architecture capability manifest, platform compatibility manifest, PC platform initialization contract, PC required device classes, kernel section protection contract, boot-module validation, boot-module address-space tracking, ELF validation, RAMFS file tools, syscall validation, syscall boundary policy, syscall definition table, syscall filter policy, syscall resource limit policy, syscall scheduling primitives, initial process contract, device registry, RAM block device, block VFS mount, framebuffer surface, linear framebuffer boot contract, device RAMFS metadata, renderer, pixel renderer contract, cursor scaffold, terminal UI, terminal style contract, TUI widgets, window manager, desktop shell prototype, fullscreen desktop mode, desktop input interactions, UI event queue, address-space, address-space protection flag, address-space paging policy diagnostics, bootstrap paging policy application, runtime paging, runtime paging policy self-test, paging, paging protection flag, PIT IRQ0, keyboard IRQ1, task stack ownership, active context switch, scheduler tick, scheduler round-robin policy and scheduler sleep/wake markers appear.

Run a lighter terminal-only boot smoke test:

```sh
scripts/tinyos-dev.sh test-terminal
# or: make test-terminal-boot
```

Run a longer runtime stability check:

```sh
make test-stability
```

By default this uses a 20 second QEMU window. Override it with `STABILITY_TEST_TIMEOUT=60s` for a longer run.

Run the current minimum runtime envelope test:

```sh
make test-minimal
```

By default this boots TinyOS with `MINIMAL_TEST_MEMORY=32M` and checks the boot, requirements, renderer, terminal UI, terminal style, scheduler round-robin policy, scheduler sleep/wake, syscall scheduling primitives, initial process contract, runtime paging policy, UI event queue and storage mount markers. Override it with `MINIMAL_TEST_MEMORY=64M` when comparing low-memory behavior.

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