# TinyOS OS Roadmap

This document turns the current implementation notes into a staged plan for growing TinyOS from a small bootable kernel into a real, stable, modular operating system that can run from ISO images first, then from USB or microSD-friendly disk images.

## Product goal

TinyOS should become a small, auditable operating system for experiments, IoT-style devices and low-resource targets. The system should favor stability, clear module boundaries and incremental bootable milestones over rapid feature growth.

The long-term system should provide:

- bootable ISO images for emulators and optical-style boot flows;
- bootable disk images for USB drives and microSD cards;
- a stable `i686` reference kernel before wider architecture support;
- a clean architecture/platform split for future `x86_64` and `aarch64` ports;
- reliable diagnostics, panic reporting and serial logs;
- interrupt-driven input and timers;
- paging, memory protection and allocator diagnostics;
- kernel tasks, then user processes;
- VFS, initrd and persistent block storage support;
- a syscall boundary with strict user pointer validation;
- an optional GUI path built on framebuffer, events and a 2D renderer.

## Development rules

1. Keep every milestone bootable in QEMU.
2. Prefer small contracts over large global subsystems.
3. Keep architecture-specific code out of generic kernel modules.
4. Add diagnostics before enabling risky runtime behavior.
5. Treat GUI as optional until memory, tasking, input and storage are stable.
6. Add tests or boot checks for every infrastructure milestone.
7. Do not introduce dynamic module loading before static module contracts are stable.

## Quality gates

Each milestone is complete only when all relevant gates pass:

- `make prepare-test-env` reports required tools clearly;
- `make iso` builds the bootable ISO;
- `make test-boot` reaches the kernel boot success marker in QEMU;
- panic paths print useful VGA and serial diagnostics;
- public subsystem headers expose narrow, documented contracts;
- the implementation does not add avoidable dependencies between `arch`, `kernel`, `drivers` and future `platform` code.

## Stage 0 - project and test foundation

Goal: make the project easy to build, diagnose and evolve safely.

Tasks:

- keep `Makefile` targets small and explicit;
- add toolchain preflight checks;
- add a headless QEMU boot smoke test;
- document local testing commands;
- keep Visual Studio Makefile integration aligned with supported Make targets;
- keep roadmap documents split by purpose: OS direction, implementation tasks and security tasks.

Completion criteria:

- missing tools produce actionable errors;
- a developer can run a one-command boot smoke test;
- boot logs include a stable success marker.

## Stage 1 - boot media foundation

Goal: keep ISO boot reliable and prepare a later disk image path.

Tasks:

- keep GRUB + Multiboot as the reference boot path for now;
- build `tinyos.iso` for QEMU and virtual machines;
- keep `tinyos-debug.iso` for serial-heavy boot diagnostics;
- design a future raw disk image flow for USB and microSD;
- document host requirements for writing images safely;
- evaluate Limine or a dedicated loader only after the current boot path is stable.

Completion criteria:

- ISO boot works in QEMU;
- debug ISO gives serial checkpoints;
- the raw image design does not require privileged host operations during normal builds.

## Stage 1B - installed system pattern

Goal: define how the ISO eventually becomes an installed TinyOS system on a virtual or physical disk.

Reference document: `docs/installed-system-pattern.md`

Tasks:

- define a terminal-first installer flow for ISO boot media;
- collect device name, network profile, primary user and administrator policy;
- keep shared user/admin bootstrap passwords limited to development or single-user profiles;
- store credentials as separate salted hashes when user and admin secrets are derived from the same installer input;
- write install receipts and first-boot profiles;
- keep the installed system able to run provisioning tools after first boot.

Completion criteria:

- install profile format is documented and host-validated;
- terminal installer mock can write a receipt without touching real disks;
- reference `i686` QEMU disk install has a repeatable smoke test before expanding to `x86_64` or `aarch64`.

## Stage 2 - interrupts, timer and input

Goal: move from polling-style interaction to controlled interrupt-driven operation.

Tasks:

- finish PIC initialization policy;
- enable hardware IRQs one source at a time;
- enable PIT ticks through IRQ0;
- enable keyboard input through IRQ1;
- keep handlers short and dispatch work into queues;
- expose stable input events to shell and future UI layers;
- add shell diagnostics for IRQ and tick state.

Completion criteria:

- timer ticks are stable under QEMU;
- keyboard input works without polling the controller forever;
- interrupt storms or unexpected IRQs are logged safely.

## Stage 3 - memory protection and allocator hardening

Goal: make memory failures detectable and prepare real isolation.

Tasks:

- strengthen frame allocator accounting;
- add heap misuse checks for double free, invalid free and corrupted blocks;
- add optional debug poison patterns;
- initialize kernel paging in a controlled mode;
- add page flag APIs for read, write, user and executable permissions;
- add guard pages for kernel stacks and critical regions when paging is ready;
- keep identity mapping assumptions explicit and temporary.

Completion criteria:

- allocator integrity checks catch common misuse cases;
- paging can be enabled without breaking boot;
- memory faults produce controlled diagnostics.

## Stage 4 - scheduler and kernel tasks

Goal: turn TinyOS from a single kernel control flow into a multitasking kernel.

Tasks:

- define a complete task descriptor;
- allocate and own one kernel stack per task;
- implement `i686` context switch code;
- add a round-robin scheduler;
- add idle, yield and sleep primitives;
- expose task diagnostics in the shell;
- keep the scheduler independent from shell internals.

## Stage 4B - application runtime and language support

Goal: define how TinyOS will grow from kernel scaffolds into a system that can run native, self-hosted and sandboxed applications.

Tasks:

- keep `tinyos-native-i686-v0` as the first native ABI for C and C++ ELF32 applications;
- expose runtime profiles through kernel diagnostics and RAMFS metadata;
- add capability-gated app profiles before executing untrusted code;
- expose app profiles through shell diagnostics and RAMFS metadata;
- reject non-ready app profiles through launch-policy dry checks before loader execution exists;
- define `.tapp` as the TinyOS app package contract before real installers and dynamic loading;
- expose package install-gate diagnostics before accepting external app payloads;
- plan sandbox runtimes such as WASM, bytecode and scripts after paging and syscall validation mature;
- keep GUI applications dependent on UI/event/syscall contracts, not direct device access.

Completion criteria:

- runtime profiles are visible in shell diagnostics;
- app profiles declare capabilities before launch;
- `.tapp` package metadata can be validated by host tools and inspected from the kernel shell;
- package verification reports signature and payload blockers before installation exists;
- native apps and future sandbox apps share the same security policy surface.

Completion criteria:

- multiple kernel tasks can run and yield;
- timer-driven scheduling is stable;
- a stuck non-critical task does not prevent panic diagnostics.

Current groundwork:

- scheduler tick accounting from PIT IRQ0;
- round-robin scheduler policy selection and validation without active context switching yet;
- sleep/wake task-state contract with wake events driven by scheduler ticks;
- active context switching remains gated on an architecture switch implementation.

## Stage 5 - VFS, initrd and storage

Goal: make files and devices first-class kernel objects.

Tasks:

- stabilize VFS node and operation contracts;
- keep RAMFS as the first test filesystem;
- make initrd module parsing stricter;
- add a block device abstraction;
- add a QEMU-friendly block driver such as ATA/IDE or VirtIO block;
- add a simple read-only filesystem path before write support;
- prepare TinyOS-native `/devices` and diagnostic virtual files gradually.

Completion criteria:

- TinyOS can read files from initrd or a block-backed filesystem;
- shell diagnostics can inspect mounted objects;
- shell tools can browse, view and edit RAMFS files before userland tools exist;
- TinyOS-native names are primary, with Unix-style command aliases treated only as compatibility helpers;
- storage drivers do not leak hardware details into VFS.

## Stage 6 - syscall boundary and userland

Goal: move shell and tools out of the kernel.

Tasks:

- freeze an initial syscall numbering policy;
- add strict validation for user pointers and buffer lengths;
- finish enough ELF loading to start one user process;
- add a minimal `init` process;
- move shell toward userland;
- add basic syscalls: `read`, `write`, `open`, `close`, `exit`, `yield`, `sleep` and `spawn`;
- keep kernel APIs separate from user ABI headers.

Completion criteria:

- the kernel starts `init`;
- `init` can start a userland shell;
- bad user arguments fail without corrupting the kernel.

Current groundwork:

- syscall table includes scheduler-backed `yield` and `sleep` primitives;
- unimplemented file, spawn and exit syscalls remain filtered or unsupported;
- initial `init` process contract records the future name, entry path and user stack envelope;
- real ELF process start and userland shell execution remain future work.

## Stage 7 - device model and drivers

Goal: let the system grow without hard-wiring every device into the kernel core.

Tasks:

- add a device registration model;
- define device classes for console, input, timer, block and framebuffer;
- make driver init ordering explicit;
- keep early boot drivers static;
- design metadata for future loadable modules;
- add driver health and failure diagnostics.

Completion criteria:

- generic kernel code talks to devices through interfaces;
- adding a driver does not require unrelated kernel changes;
- device state is visible through diagnostics.

Current groundwork:

- static registry for boot console, diagnostics, interrupt controller, timer, input queue, keyboard, RAMFS, RAM block storage and VGA text-grid surface;
- `devices` and `device <name>` shell diagnostic commands;
- `blockinfo` diagnostics for the RAM-backed block scaffold;
- read-only `/volumes/ram-block0/volume.txt` path backed by block sector 0;
- `storageinfo` diagnostics for block VFS mount state;
- `fbinfo` diagnostics for the active text-grid surface scaffold;
- RAMFS `/devices` metadata files for device-oriented file tools;
- persistent block storage and a linear framebuffer mode are reserved for later implementations.

## Stage 8 - security and isolation

Goal: reduce the impact of bugs and prepare permission-aware applications.

Tasks:

- enforce user/kernel memory separation;
- validate every syscall argument;
- add process identities and basic permission flags;
- add resource limits for memory, files and tasks;
- validate module and initrd metadata;
- add checksums for trusted boot resources;
- prepare syscall filtering when process metadata exists.

Completion criteria:

- a faulty user process cannot overwrite kernel memory;
- resource exhaustion fails in controlled ways;
- module loading policy is explicit, even if modules remain static.

Current groundwork:

- runtime paging is enabled on the reference `i686` boot path;
- address-space policy is applied to bootstrap page tables before runtime use;
- runtime paging policy self-test validates CR3 and kernel/module page flags before shell entry;
- userspace isolation and permission enforcement remain future work.

## Stage 9 - optional GUI path

Goal: add graphical capability without making it required for debugging.

Tasks:

- add a framebuffer abstraction;
- add a tiny software 2D renderer;
- add bitmap font rendering;
- route keyboard and future mouse input through event queues;
- build a graphical terminal first;
- add TUI widgets before a window manager;
- keep VGA text mode available as a fallback.

Completion criteria:

- text mode remains usable;
- graphical mode can render a terminal and receive input;
- GUI code depends on device and event contracts, not raw hardware.

Current groundwork:

- text-grid renderer scaffold on the active VGA surface;
- text-grid fill/clear renderer primitives;
- `renderinfo`, `rendertest` and `renderfilltest` shell diagnostics;
- terminal UI scaffold with a status row and content region;
- terminal-first operational diagnostics through `sysinfo`, `status`, `syscheck`, `riskinfo`, `profileinfo`, `profilecheck`, `pathcheck` and `helpsearch`;
- current `filemgr` two-pane RAMFS manager with view, create, edit, remove, copy-to-other-pane and move-to-other-pane actions;
- current `textedit` interactive RAMFS editor with load, replace, append, clear, save, reload and metadata actions;
- lighter `fileui` RAMFS browser with view, create, edit, remove, copy and move actions;
- terminal clear/panel helpers over renderer primitives;
- `terminalinfo`, `terminaltest`, `terminalclear` and `terminalpaneltest` shell diagnostics;
- terminal style/color contract with segmented text rendering and `terminalstyle` diagnostics;
- first TUI widget scaffold with label and button drawing;
- widget event dispatch bridge for activation keys;
- `widgetinfo`, `widgettest`, `widgetdispatch` and `widgetactiontest` shell diagnostics;
- UI event queue scaffold over the generic input queue;
- `uieventinfo`, `uieventpump`, `uieventpeek` and `uieventtest` shell diagnostics;
- color terminal themes for provisioning and diagnostics are planned on top of the stable text-grid style helpers;
- framebuffer-backed higher-resolution terminal modes remain future work.

## Stage 9B - project provisioning workbench

Goal: make TinyOS easier to prepare for a concrete project or device family without turning provisioning into a monolithic installer.

Tasks:

- create isolated project workspace folders with plain-text profiles;
- configure signing, encryption, remote access, API exposure and terminal preferences per project;
- support multiple device variants with RAM, ROM/image, display, storage and feature budgets;
- add resource diagnostics that combine host image size, `.tapp` package size and kernel memory counters;
- keep remote access opt-in, starting with host-side SSH/SCP/SFTP and later a TinyLink target transport;
- expose the workflow through a colorful terminal UI after text-grid color helpers are stable.

Completion criteria:

- `scripts/tinyos-image.sh provision-plan` explains the workflow;
- a project profile can be validated before building an image;
- resource checks can reject a variant before deployment;
- remote deploy still requires signing and encryption evidence by default.

## Minimum runtime envelope

Current tested minimum:

- `i686` compatible 32-bit CPU;
- BIOS-style QEMU PC boot path with GRUB Multiboot ISO;
- 32 MiB RAM for the current kernel scaffolds, with 128 MiB recommended for development experiments;
- VGA text mode 80x25;
- PS/2 keyboard controller;
- 8259 PIC and 8253/8254 PIT;
- ISO boot media plus RAM-backed block storage scaffold.

The `make test-minimal` target boots the current ISO with `MINIMAL_TEST_MEMORY=32M` and checks the low-memory smoke markers. The `make test-minimal-probe` target can probe lower values such as 24 MiB and 16 MiB before the documented baseline is changed. The `make test-lowmem-probe` target uses terminal markers for sub-16 MiB and KiB-scale experiments; the current desktop-capable reference ISO passes at `2561K` and fails at `2560K` and `64K` before TinyOS serial output begins. The `TERMINAL_ONLY=1` profile, available through `make terminal-only-iso` and `make test-terminal-lowmem-probe`, omits desktop/window-manager/cursor/mouse objects and currently reaches `2529K` while `2528K` and `64K` still fail before TinyOS serial output begins.

## Stage 10 - portability

Goal: grow beyond `i686` only after the reference target is stable.

Tasks:

- define the final `arch` contract from the stable `i686` implementation;
- introduce `platform/pc` for machine-specific PC behavior;
- add `x86_64` as the first wider CPU backend;
- add `platform/qemu-x86_64`;
- add `aarch64` through QEMU `virt`;
- defer physical boards until emulator targets have repeatable tests.

Completion criteria:

- most kernel modules compile without architecture-specific assumptions;
- a second architecture can boot to early diagnostics;
- platform code is separate from CPU code.

## Version milestones

### v0.2 - stable developer baseline

- toolchain preflight checks;
- QEMU smoke test target;
- serial boot marker;
- roadmap and testing documentation.

### v0.3 - interrupt rollout

- stable IDT and exception paths;
- IRQ diagnostic counters and shell commands;
- PIT configuration diagnostics;
- PIC policy;
- PIT IRQ0 runtime path and QEMU stability marker;
- keyboard IRQ1 runtime path with polling fallback;
- IRQ diagnostics.

### v0.4 - memory hardening

- stronger frame allocator accounting;
- heap misuse detection;
- paging interfaces refined;
- controlled paging enablement experiment.

### v0.5 - kernel tasking

- task descriptors;
- scheduler tick accounting from PIT IRQ0;
- task and scheduler diagnostics;
- yield and sleep scaffolds;
- kernel stacks;
- i686 context ABI scaffold;
- context switching;
- round-robin scheduler;
- idle, yield and sleep.

### v0.6 - files and boot resources

- stable RAMFS and VFS contracts;
- stricter initrd metadata parsing;
- block device abstraction;
- first read-only storage path.

### v0.7 - userland boundary

- syscall validation;
- ELF process start path;
- `init` process;
- userland shell prototype.

### v0.8 - hardware image path

- raw disk image design implemented;
- USB or microSD write documentation;
- emulator disk boot test;
- safer image generation workflow.

### v0.9 - graphical experiment

- framebuffer abstraction;
- 2D renderer;
- graphical terminal;
- text-mode fallback preserved.

### v1.0 - stable demo system

- ISO and disk image boot paths;
- protected kernel memory baseline;
- kernel tasks and first user processes;
- VFS with a real boot resource path;
- basic storage path;
- stable diagnostics and QEMU test flow;
- optional GUI mode treated as experimental unless fully stable.

## Immediate next steps

1. Finish Stage 0 with preflight and QEMU smoke tests.
2. Stabilize Stage 2 by enabling timer IRQs before keyboard IRQs.
3. Harden heap diagnostics before enabling more runtime complexity.
4. Keep ISO boot as the primary test loop.
5. Design `.img` generation after storage and boot media assumptions are explicit.