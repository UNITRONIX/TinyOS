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
| 2 | `test-host` | Host unit tests for pure-logic modules (path canonicalisation) |
| 3 | `test-stability` | Boot smoke + longer QEMU window (`STABILITY_TEST_TIMEOUT`, default 20s) |
| 4 | `test-boot-x86_64` | 64-bit bring-up still reaches long mode with a valid GDT/TSS/IDT |
| 5 | `test-memory-scaling` | Frame pool tracks installed RAM at 32M, 512M and 2G |
| 6 | `test-minimal` | Minimum RAM envelope still boots |
| 7 | `test-disk-boot` | Raw disk boot path still works |
| 8 | `install-profile-check` | Install profile contract (non-destructive) |
| 9 | `tapp-trust-test` | Package trust / verification scaffold |

Add wave-specific targets when the change touches those areas:

| Change area | Extra automated tests |
|-------------|----------------------|
| Memory / paging / low RAM | `make test-memory-scaling`, `make test-minimal`, optionally `make test-minimal-probe` |
| Image / provisioning | `make image-deploy-check-test`, `make tapp-sign-test` |
| Scheduler / preemption | `make test-boot` verifies the context-switch and watchdog markers via `tests/boot-markers.txt` |
| Storage / block devices | `make test-virtio-block`, `make test-ata-block` |
| x86_64 backend | `make test-boot-x86_64` (contract in `tests/boot-markers-x86_64.txt`) |

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

## Host unit tests

`make test-host` compiles pure-logic modules for the host and runs them, with no
emulator involved. They exist because security-relevant validators used to be
testable only by booting the kernel and typing at a shell.

| File | Covers |
|------|--------|
| `tests/host/test_path.cpp` | `libk/path.cpp`: canonical absolute paths, dot segments, repeated and trailing separators, shell metacharacters, embedded control characters, segment and path limits, prefix rules |

Anything that is a pure function over data belongs here rather than in a boot
marker. If a parser has no kernel dependency, extract it into `libk/` and add a
host test.

## Continuous integration

`.github/workflows/ci.yml` runs the same sequence locally:

```sh
scripts/ci-local.sh
```

CI executes host unit tests, both architecture builds and boot smoke tests,
memory scaling, VirtIO and ATA storage boot tests, and the host-side install and
package-trust contracts. Boot logs are uploaded as artefacts on every run.

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

The smoke test captures serial output through QEMU's serial file backend in `build/boot-smoke.log` and passes when QEMU keeps running until the timeout and every marker listed in `tests/boot-markers.txt` appears.

That marker file is the single source of truth for boot contracts. Add a line to it whenever a new subsystem starts publishing a `klog` readiness marker, and remove the line when the subsystem goes away. Keeping the list in a file avoids the unmaintainable chained `grep` expressions the Makefile used to carry.

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

Probe sub-16 MiB and KiB-scale RAM values with core boot markers:

```sh
make test-lowmem-probe
make test-lowmem-probe LOWMEM_PROBE_MEMORY="3M 2624K 2561K 2560K 2M 64K"
```

This target checks only the core boot, system requirements and terminal UI markers. The reference QEMU/GRUB ISO probe currently passes at `2561K` and fails at `2560K` and `64K` with no TinyOS serial output. The kernel links at `1M`, so a `64K` machine cannot run this image at all; nearby KiB values can also be non-monotonic because the failure happens inside GRUB before TinyOS logging starts.

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

Run headless with the serial console on stdout:

```sh
make run-headless
# or: scripts/tinyos-dev.sh run-serial
```

Run the debug boot image with serial checkpoints:

```sh
make debug-run
```

## Current limitation

The project builds an ISO image. `make disk-image` produces a hybrid boot disk image, and `make virtio-disk-image` produces a data disk for the VirtIO block test path.
