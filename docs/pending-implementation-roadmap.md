# TinyOS Pending Implementation Roadmap

This document lists every feature, subsystem, tool and infrastructure item that still requires real implementation in TinyOS. It is a consolidated backlog derived from the current source tree, kernel manifests, shell contracts and the existing roadmap documents.

Related documents:

- `docs/plan-dojrzalosci-systemu.md` — plan wdrożenia dojrzałości (funkcjonalność, bezpieczeństwo, użyteczność)
- `docs/os-roadmap.md` — strategic product direction
- `docs/implementation-roadmap.md` — phase checklist with done and pending items mixed
- `docs/security-roadmap.md` — security program
- `docs/installed-system-pattern.md` — installer and installed-system contract
- `docs/provisioning-workbench.md` — developer provisioning workflow
- `docs/user-management-tools.md` — shell tool inventory
- `docs/secure-image-provisioning.md` — image signing and deployment pipeline

## How to read this document

Each item is tagged with one of these states:

| Tag | Meaning |
|-----|---------|
| **Not started** | No runtime implementation exists; only docs or manifest entries may exist |
| **Scaffold only** | Code boots, self-tests or dry-checks exist, but the feature does not perform its real job |
| **Partial** | Some behavior exists, but the milestone is incomplete |
| **Host only** | Works on the development host through `scripts/tinyos-image.sh`, not in the kernel |

Priority bands:

| Priority | Meaning |
|----------|---------|
| **P0** | Blocks core OS behavior; other work depends on it |
| **P1** | Required for a usable installed system or userland |
| **P2** | Important for portability, provisioning or production readiness |
| **P3** | Optional, experimental or long-term |

## Executive summary

TinyOS now includes IRQ preemption, GDT/TSS ring-3 embedded init with `int 0x80` syscalls, ATA PIO + FAT16, hybrid boot-disk scripting, Multiboot2 header, USB HID UHCI probe, VirtIO-net detection and a hashed account store. Remaining high-impact work is full USB HID transfers, AHCI/NVMe, per-process address spaces, a real userland rootfs and a VirtIO-net datapath/TCP stack.

The highest-impact pending work is:

1. Active context switching and preemptive scheduling (**P0**)
2. Persistent block storage and mountable filesystem (**P0**)
3. ELF load, ring-3 entry and real `init` process (**P0**)
4. Full syscall dispatch for file and process operations (**P0**)
5. Architecture/platform split and multi-arch boot (**P1**)
6. Terminal installer, credentials and persistent configuration (**P1**)
7. Networking stack and network configuration tools (**P1**)
8. Target-side package/image verification and rollback (**P2**)

---

## 1. Kernel execution model

### 1.1 Scheduler and context switching — P0

| Item | Status | Notes |
|------|--------|-------|
| Active `i686` context switch | Scaffold only | `arch/i686/context.cpp`: `context_switch_available()` returns `false` |
| Wire round-robin policy to real task switching | Not started | Scheduler counts ticks and policy decisions but never switches stacks |
| Preemptive scheduling on PIT tick | Not started | Depends on context switch |
| Guard pages for kernel task stacks | Not started | Planned in `docs/os-roadmap.md` Stage 3 |
| Task watchdog / stuck-task timeout | Not started | Planned in `docs/security-roadmap.md` Stage 3 |
| SMP / multi-core scheduling | Not started | Not yet documented in detail; required later for modern x86_64 |

### 1.2 Syscalls and userspace — P0

| Item | Status | Notes |
|------|--------|-------|
| Ring-3 user entry path | Scaffold only | `kernel/user/transition.cpp`: `init_launch_supported()` returns `false` |
| Syscall dispatch from user mode | Not started | ABI and validation exist; no user trap path |
| Implement `read` syscall | Not started | Defined but not implemented |
| Implement `write` syscall | Not started | Defined but not implemented |
| Implement `open` syscall | Not started | Defined but not implemented |
| Implement `close` syscall | Not started | Defined but not implemented |
| Implement `spawn` syscall | Not started | Defined but not implemented |
| Implement `exit` syscall | Not started | Defined but not implemented |
| Connect `yield` and `sleep` to user trap path | Partial | Kernel-side primitives exist; user dispatch missing |
| User pointer validation against real page tables | Partial | Validation scaffold exists; no ring-3 mappings yet |
| Syscall filtering enforcement per process | Scaffold only | Filter policy exists; no process metadata |
| Syscall resource limit enforcement | Scaffold only | Resource policy exists; no runtime accounting |
| Separate user ABI headers from kernel headers | Not started | Planned in Stage 6 |

### 1.3 ELF loader and process launch — P0

| Item | Status | Notes |
|------|--------|-------|
| Load ELF32 program segments into address space | Not started | `kernel/elf/loader.cpp` validates headers only |
| Create user stack and entry context | Not started | Contract exists in user transition scaffold |
| Start `init` as first real user process | Not started | Contract records name, entry path and stack envelope |
| Application launcher runtime execution | Scaffold only | `kernel/app/launcher.cpp` returns `RuntimeNotReady` / `AppNotReady` |
| Connect launch-policy checks to real launcher | Not started | Dry-checks exist today |
| Move shell out of kernel into userland | Not started | Shell currently runs inside the kernel |

---

## 2. Memory management

### 2.1 Paging and address spaces — P1

| Item | Status | Notes |
|------|--------|-------|
| Complete paging and address-space abstractions | Partial | Bootstrap identity map enabled; userspace mappings missing |
| Per-process address spaces | Not started | Required before user processes |
| Remove long-term identity-map assumption | Not started | Explicitly temporary in os-roadmap Stage 3 |
| NX / W^X enforcement | Not started | `arch::Info.nx_supported = false` on i686 |
| Guard pages for critical kernel regions | Not started | Planned after paging matures |

### 2.2 Allocator hardening — P1

| Item | Status | Notes |
|------|--------|-------|
| Debug heap poison patterns | Not started | Planned in os-roadmap Stage 3 |
| Expanded `WARN_ON` categories for drivers | Partial | Memory warnings exist; driver/security categories incomplete |
| Stack canaries | Not started | Listed in security-roadmap Stage 7 |
| Stronger frame allocator accounting | Partial | Basic allocator exists; hardening incomplete |

---

## 3. CPU, interrupts and boot

### 3.1 i686 foundation — P1

| Item | Status | Notes |
|------|--------|-------|
| GDT cleanup / review | Not started | Open item in implementation-roadmap Phase 1 |
| APIC support (replace legacy PIC path) | Not started | Needed for modern PC and x86_64 |
| Power off / shutdown | Not started | Only `reboot` exists today |
| Real-time clock (RTC / CMOS) | Not started | Only PIT uptime exists |
| ACPI basics (MADT, FADT, reboot/power) | Not started | Needed for physical x86 machines |
| PCI enumeration | Not started | Needed for storage, network and USB later |

### 3.2 Boot media — P1

| Item | Status | Notes |
|------|--------|-------|
| Raw disk image generation (`.img`) | Not started | Planned in os-roadmap Stage 1 and v0.8 |
| QEMU disk-boot smoke test | Not started | ISO boot only today |
| USB / microSD write workflow documentation and tooling | Not started | Planned in v0.8 |
| Multiboot2 entry path | Not started | Current boot uses Multiboot v1 |
| UEFI boot support | Not started | Required for many physical machines and x86_64 |
| Evaluate Limine or dedicated second-stage loader | Not started | Deferred until current boot path is stable |
| Persistent disk image creation in host tools | Not started | Host-side install milestone |

### 3.3 Initrd and boot modules — P1

| Item | Status | Notes |
|------|--------|-------|
| Mount initrd module contents into VFS | Scaffold only | Metadata parsing works; placeholder module only |
| Load files from boot modules at runtime | Not started | Required for boot resources and early apps |
| Stricter initrd payload validation at use time | Partial | Metadata validation exists |

---

## 4. Storage, VFS and filesystems

### 4.1 Block devices — P0

| Item | Status | Notes |
|------|--------|-------|
| Real persistent block device driver | Not started | Only 8×512-byte RAM block exists |
| VirtIO block driver | Not started | Recommended first QEMU-friendly driver |
| ATA/AHCI driver | Not started | Needed for physical PC storage |
| DMA support for block devices | Not started | Required for real disk I/O |
| Write-through block file experiments | Not started | Open item in implementation-roadmap |
| Block device health and failure diagnostics | Partial | Registry exists; real driver diagnostics missing |

### 4.2 Filesystems and mounts — P0

| Item | Status | Notes |
|------|--------|-------|
| `mount` shell command | Planned | Listed in `kernel/admin/tools.cpp` as `State::Planned` |
| Writable block-backed filesystem | Not started | Block VFS is read-only under `/volumes/` |
| TinyOS-native persistent filesystem or ext2 reader | Not started | RAMFS only today; data lost on reboot |
| Persistent `/system`, `/users`, `/apps` layout on disk | Not started | Documented in installed-system-pattern |
| VFS write path to non-RAM backends | Not started | RAMFS write works; block mount does not |
| Permission enforcement on VFS nodes | Not started | `chmod` changes metadata only |

---

## 5. Device model and drivers

### 5.1 Console and input — P1

| Item | Status | Notes |
|------|--------|-------|
| Console backend abstraction (VGA / serial / FB text) | Not started | Shell is tied to VGA today |
| Serial-only headless console profile | Partial | Serial logging exists; not primary interactive console |
| USB HID keyboard driver | Not started | PS/2 only; insufficient for many PCs |
| VirtIO input driver | Not started | Useful for QEMU and cloud targets |
| PS/2 mouse hardware driver completion | Partial | Mouse scaffold exists in desktop builds |
| Keyboard layout / locale selection | Not started | Installer expects this later |
| Language selection in installer | Not started | Documented in installed-system-pattern |

### 5.2 Platform detection — P1

| Item | Status | Notes |
|------|--------|-------|
| Top-level `platform/` directory split | Not started | Code currently lives under `kernel/platform/` |
| Machine profile probing at boot | Not started | QEMU vs physical vs ARM detection |
| `machineinfo` / expanded `compatcheck` diagnostics | Not started | Useful for multi-machine support |
| Device Tree parsing (ARM) | Not started | Required for `aarch64` |
| Dynamic driver init ordering beyond static registry | Not started | Static registration only today |
| Loadable kernel modules | Not started | Explicitly deferred until static contracts are stable |

### 5.3 Other hardware classes — P2

| Item | Status | Notes |
|------|--------|-------|
| Linear framebuffer mode through bootloader metadata | Partial | Optional GRUB framebuffer preview exists |
| Bitmap font rendering on framebuffer | Not started | Planned in os-roadmap Stage 9 |
| USB storage | Not started | Needed for USB boot media workflows |
| Framebuffer-backed high-resolution terminal | Not started | Planned in provisioning workbench |

---

## 6. Networking — P1

| Item | Status | Notes |
|------|--------|-------|
| Network device model | Not started | `network.mode=disabled` is the default profile |
| Network driver (VirtIO net, e1000 or similar) | Not started | No driver code exists |
| Layer-2 / layer-3 stack | Not started | No TCP/IP implementation |
| DHCP client | Not started | Fields exist in install profile |
| Static IP configuration | Not started | Fields exist in install profile |
| DNS client | Not started | Fields exist in install profile |
| `netinfo` shell command | Planned | Manifest only |
| `netconfig` shell command | Planned | Manifest only |
| `hostname` shell command | Planned | Manifest only |
| TinyLink target transport | Not started | Planned successor to SSH-only provisioning |
| Socket API or syscall surface | Not started | Required for networked apps and remote provisioning |

---

## 7. Security and trust

### 7.1 Isolation and permissions — P0

| Item | Status | Notes |
|------|--------|-------|
| Userspace memory isolation | Not started | Security-roadmap open item |
| Process permission model | Not started | Capability masks exist at manifest level only |
| Resource limits for memory, files and tasks | Scaffold only | Policies exist; enforcement missing |
| Safer driver failure handling policy | Partial | Panic path exists; driver-specific recovery incomplete |

### 7.2 Identity and credentials — P1

| Item | Status | Notes |
|------|--------|-------|
| Password hashing | Not started | Policy documented; no runtime |
| Install-time credential storage | Not started | Profile fields exist; no secure store |
| Separate user and administrator identities | Not started | Documented in installed-system-pattern |
| `useradd` shell command | Planned | Manifest only |
| `passwd` shell command | Planned | Manifest only |
| `whoami` shell command | Planned | Manifest only |
| `id` shell command | Planned | Manifest only |
| First-boot forced password change for development profiles | Not started | Documented only |

### 7.3 Module, package and image trust — P1

| Item | Status | Notes |
|------|--------|-------|
| Target-side `.tapp` cryptographic verification | Scaffold only | Host `verify-app` exists; kernel does dry-checks |
| Target-side image signature verification | Not started | Host signing exists |
| Move trust anchors to persistent replaceable storage | Not started | Trust store uses planned placeholder anchors |
| Release/image/recovery root keys in secure storage | Planned | `kernel/security/trust.cpp` entries are `State::Planned` |
| Trusted module loading policy enforcement | Partial | Validation scaffolds exist |
| Checksums for trusted boot resources | Not started | Planned in os-roadmap Stage 8 |
| Connect signed image verification to provisioning agent | Not started | Security-roadmap near-term priority |
| Interactive confirmation for high-risk shell commands | Not started | `riskinfo` lists commands; no prompt gate |

---

## 8. Applications and runtimes

### 8.1 Native applications — P0

| Item | Status | Notes |
|------|--------|-------|
| Execute native C/C++ ELF32 applications | Not started | Runtime manifest marked Ready, loader not complete |
| Install `.tapp` packages to runtime storage | Not started | Registry and verifier scaffolds only |
| `tappinstall` shell command | Planned | Manifest only |
| `tappremove` shell command | Planned | Manifest only |
| `package` shell command | Planned | Manifest only |
| Example app `example-system-tool` execution | Planned | Manifest entry is `State::Planned` |

### 8.2 Planned application profiles — P2

| Item | Status | Notes |
|------|--------|-------|
| `desktop-shell` native GUI app | Planned | Manifest only |
| `selfhost-toolchain` native app | Planned | Manifest only |
| `web-gui-host` WASM app | Planned | Manifest only |
| `bytecode-service` bytecode app | Planned | Manifest only |

### 8.3 Sandbox runtimes — P3

| Item | Status | Notes |
|------|--------|-------|
| `wasm32-sandbox` runtime | Planned | Manifest in `kernel/app/runtime.cpp` |
| `tiny-bytecode` runtime | Planned | Manifest only |
| `tiny-script` runtime | Planned | Manifest only |
| WASM interpreter/JIT sandbox | Not started | |
| Bytecode VM with capability enforcement | Not started | |
| Script bundle loader and executor | Not started | |

---

## 9. Terminal, shell and UI

### 9.1 Shell UX — P1

| Item | Status | Notes |
|------|--------|-------|
| Command history (up/down) | Not started | |
| Tab completion for commands and paths | Not started | |
| In-line editing (Home/End, insert/delete) | Not started | |
| Terminal scrollback buffer | Not started | |
| Persistent shell preferences (`/system/tinyos.conf`) | Not started | |
| `terminaltheme` shell command | Planned | Manifest only |
| `videomode` shell command | Planned | Manifest only |

### 9.2 Terminal Environment (TTE) — P1

| Item | Status | Notes |
|------|--------|-------|
| Unified terminal session with status bar as default boot UX | Partial | Scaffolds in `ui/terminal.cpp`; raw prompt is default |
| Refactor monolithic `shell/shell.cpp` into modules | Not started | ~5100 lines today |
| Move `filemgr` / `textedit` onto shared widget toolkit | Partial | Functional but hand-drawn |
| Serial mirror for interactive console output | Not started | klog uses serial; shell output does not mirror |

### 9.3 GUI stack — P3

| Item | Status | Notes |
|------|--------|-------|
| Complete 2D software renderer primitives | Partial | Basic primitives exist |
| Text rendering on framebuffer | Not started | Open item in implementation-roadmap Phase 7 |
| Reusable GUI widget set | Not started | TUI widget demo only |
| Production window manager | Scaffold only | Alpha text-grid WM |
| Production desktop shell | Scaffold only | Optional preview build |
| Graphical terminal | Not started | Planned in os-roadmap Stage 9 |
| Color terminal themes for provisioning UI | Not started | Style helpers exist; theme system missing |
| Framebuffer-backed higher-resolution terminal modes | Not started | |

---

## 10. Installed system and installer

Reference: `docs/installed-system-pattern.md`

### 10.1 Installer — P1

| Item | Status | Notes |
|------|--------|-------|
| Real terminal installer (not RAMFS mock) | Scaffold only | `install` writes receipt only |
| Disk target selection and partition policy | Not started | |
| Filesystem creation and system image write | Not started | |
| Install receipt and rollback metadata on disk | Not started | |
| First-boot profile on persistent storage | Not started | `/system/profile.txt` is RAMFS-only |
| Non-interactive install profile execution | Not started | Planned later |
| Language and keyboard profile selection in installer | Not started | |
| Network configuration step in installer | Not started | Depends on networking |
| User and administrator policy collection | Partial | Profile format exists; no runtime storage |
| QEMU `i686` disk-install smoke test | Not started | |

### 10.2 Post-install system — P1

| Item | Status | Notes |
|------|--------|-------|
| Boot from installed disk | Not started | ISO boot only |
| Persistent logs under `/logs/` | Not started | |
| Persistent receipts under `/receipts/` | Not started | |
| First-boot summary screen | Not started | Documented expected content |
| Provisioning tools runnable from installed system | Not started | |

---

## 11. Provisioning and image pipeline

Reference: `docs/provisioning-workbench.md`, `docs/secure-image-provisioning.md`

### 11.1 Host tooling — P2

| Item | Status | Notes |
|------|--------|-------|
| `provisioninit` — create isolated project workspace | Host only / Planned | `provision-plan` exists |
| `provisionconfig` — project defaults editor | Host only / Planned | |
| `provisionvariant` — device variant manager | Host only / Planned | |
| `provisionresources` — RAM/ROM budget estimation | Host only / Planned | |
| Host `imagebuild` beyond current `build` wrapper | Partial | Basic build exists |
| Host `imagesign` integration as named workflow step | Partial | Signing helpers exist |
| Host `imageencrypt` integration as named workflow step | Partial | age encryption exists |
| Host `remoteaccess` configuration tool | Not started | |
| Workspace layout generator (`project.tinyos/`) | Not started | Documented only |

### 11.2 Kernel / target provisioning — P2

| Item | Status | Notes |
|------|--------|-------|
| `provisionui` — color TUI workbench | Planned | Manifest only |
| `provisionapi` — project API surface exposure | Planned | Manifest only |
| `provision` — activate image on target | Planned | Manifest only |
| `deploy` — target-side or assisted deploy command | Planned | Manifest only |
| `rollback` — restore previous image slot | Planned | Manifest only |
| `imagebuild` / `imagesign` / `imageencrypt` / `keygen` shell commands | Planned | Host-side partial equivalents exist |
| Target verify before image activation | Not started | `target-verify` is `KernelPlanned` |
| Rollback slot storage and activation policy | Not started | `rollback-slot` is `KernelPlanned` |
| Resource budget diagnostics combining host and kernel facts | Not started | Documented in provisioning workbench |
| TinyLink minimal signed transport | Not started | Documented as post-networking transport |

---

## 12. Process and service management — P1

| Item | Status | Notes |
|------|--------|-------|
| `ps` shell command | Planned | Manifest only |
| `kill` shell command | Planned | Manifest only |
| `service` shell command | Planned | Manifest only |
| Process descriptors beyond kernel tasks | Not started | |
| Service supervisor or init-managed services | Not started | |

---

## 13. Multi-architecture and portability — P1

Reference: implementation-roadmap Phase 9, os-roadmap Stage 10

### 13.1 Build system — P1

| Item | Status | Notes |
|------|--------|-------|
| `ARCH=` / `PLATFORM=` build matrix in Makefile | Not started | Hardcoded `i686` today |
| Per-arch linker scripts | Not started | Single `build/linker.ld` |
| Per-arch QEMU run and test targets | Not started | `qemu-system-i386` only |
| Boot smoke tests parameterized by architecture | Not started | |

### 13.2 x86_64 — P1

| Item | Status | Notes |
|------|--------|-------|
| `arch/x86_64/` CPU backend | Not started | |
| Long mode boot entry | Not started | |
| 4-level paging with NX | Not started | |
| SYSCALL/SYSRET or INT 0x80 user entry | Not started | |
| `platform/qemu-x86_64` | Not started | |
| x86_64 context switching | Not started | |
| x86_64 ISO boot in QEMU | Not started | Milestone v0.9 |

### 13.3 aarch64 — P2

| Item | Status | Notes |
|------|--------|-------|
| `arch/aarch64/` CPU backend | Not started | |
| QEMU `virt` boot path | Not started | |
| ARM Generic Timer driver | Not started | |
| GICv2/v3 interrupt controller | Not started | |
| PL011 serial console driver | Not started | Primary early console on ARM |
| MMU bring-up for aarch64 | Not started | |
| Device Tree boot parsing | Not started | |
| UEFI or bare-metal ARM boot entry | Not started | |
| Physical board support (Raspberry Pi and similar) | Not started | Deferred until QEMU virt is stable |

### 13.4 Code structure — P1

| Item | Status | Notes |
|------|--------|-------|
| Move `kernel/platform/` to top-level `platform/` | Not started | Documented target layout |
| Create top-level `apps/` for userland | Not started | Shell still in kernel tree |
| Rename or expand `core/` into documented `libk/` | Not started | `libk/` directory does not exist |
| Use `uintptr_t` consistently for addresses in portable code | Partial | Needed for i686/x86_64 sharing |

---

## 14. Testing and developer infrastructure — P2

| Item | Status | Notes |
|------|--------|-------|
| `make test-minimal-probe` convenience target | Partial | Documented; may need Makefile alias cleanup |
| Multi-arch boot test matrix | Not started | |
| Disk-boot CI smoke test | Not started | |
| `make compat-report` generation | Not started | Proposed compatibility summary |
| Test boot profile split for terminal-only vs desktop builds | Partial | Desktop markers checked even in some infra tests |
| Visual Studio project alignment with all Make targets | Partial | VS Makefile project exists |

---

## 15. Deferred by design

These items are intentionally not scheduled until earlier contracts are stable:

| Item | Reason |
|------|--------|
| Dynamic kernel module loading | Deferred in os-roadmap development rules |
| WASM / bytecode / script runtimes | After paging, syscalls and native launch work |
| Full GUI as default boot path | Terminal-first product direction |
| Physical ARM boards | After QEMU `virt` repeatable tests |
| Custom bootloader replacement | GRUB/Limine evaluation deferred |
| SMP | After single-core scheduler is complete |
| Self-hosting toolchain inside TinyOS | Long-term; manifest entry only |

---

## 16. Recommended implementation order

This order respects the phase dependencies documented in `docs/implementation-roadmap.md`.

### Wave 1 — make the kernel actually multitask (P0)

1. Implement active `i686` context switch
2. Wire round-robin scheduler to context switch
3. Add preemptive scheduling on PIT tick
4. Add guard pages for kernel stacks

### Wave 2 — storage and boot persistence (P0)

5. VirtIO block driver
6. Read-only filesystem on block device
7. `mount` command and VFS backend selection
8. Initrd module mount into VFS
9. Raw disk image generation and QEMU disk-boot test

### Wave 3 — userland boundary (P0)

10. Per-process paging and user mappings
11. Ring-3 entry and syscall trap path
12. Implement `read`, `write`, `open`, `close`, `exit`, `spawn`
13. ELF segment loading and process creation
14. Start real `init` process
15. Port shell to userland

### Wave 4 — terminal product polish (P1)

16. Console backend abstraction
17. Shell history, tab completion and scrollback
18. TTE default session with status bar
19. High-risk command confirmation prompts
20. `terminaltheme` and persistent terminal preferences

### Wave 5 — installed system (P1)

21. Writable persistent filesystem
22. Password hashing and credential store
23. Real terminal installer
24. First-boot persistent profile
25. `hostname`, `whoami`, `id`, `passwd`, `useradd`

### Wave 6 — portability (P1)

26. `platform/` split and Makefile `ARCH=` matrix
27. x86_64 boot + paging + context switch
28. aarch64 QEMU virt serial boot
29. Console/input abstractions for non-VGA targets

### Wave 7 — networking and provisioning (P2)

30. Network driver and basic IP stack
31. `netinfo`, `netconfig`, DHCP/static config
32. Target-side `.tapp` and image verification
33. Rollback slot and `provision` activation
34. Host provisioning workbench commands
35. TinyLink transport

### Wave 8 — applications and optional GUI (P2–P3)

36. Native app launch and `tappinstall`
37. `ps`, `kill`, `service`
38. Framebuffer text rendering and graphical terminal
39. Sandbox runtimes (WASM, bytecode, script)
40. Production GUI widget set and desktop shell

---

## 17. Counts snapshot

Approximate backlog size as of TinyOS v0.1.0:

| Category | Pending items |
|----------|---------------|
| Kernel execution model | 22 |
| Memory management | 9 |
| CPU, interrupts and boot | 18 |
| Storage and VFS | 11 |
| Device model and drivers | 17 |
| Networking | 11 |
| Security and trust | 18 |
| Applications and runtimes | 14 |
| Terminal, shell and UI | 17 |
| Installed system and installer | 14 |
| Provisioning and image pipeline | 18 |
| Process and service management | 5 |
| Multi-architecture and portability | 22 |
| Testing and developer infrastructure | 6 |
| **Total tracked items** | **~202** |

---

## 18. Maintenance rules

1. When an item is implemented, remove it from this document and mark it complete in `docs/implementation-roadmap.md`.
2. When a new scaffold is added with no real runtime behavior, add it here immediately.
3. Keep `[~]` partial items in both documents until they are fully complete.
4. Do not move deferred-by-design items into active waves unless the team explicitly promotes them.
5. Prefer bootable increments: every wave should leave `make iso`, `make test-boot` and the terminal shell usable.
