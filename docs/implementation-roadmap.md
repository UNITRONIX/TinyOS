# TinyOS Implementation Roadmap

Strategic OS roadmap: `docs/os-roadmap.md`

## Current baseline

TinyOS currently provides:

- `GRUB`-bootable `i686` kernel
- text-mode `VGA` console
- polling-based keyboard input
- minimal shell and system API layer
- external-toolchain build through `Makefile`

## Current implementation status

- [x] Phase 0 - baseline hardening
- [x] Phase 1 - CPU exception foundation
- [x] Phase 2 - PIT IRQ0 and keyboard IRQ1 enabled with diagnostics and polling fallback
- [x] Phase 3 - memory map, allocator, heap and address-space scaffolds implemented
- [~] Phase 4 - task and scheduler runtime scaffolds active, context switch not implemented yet
- [x] Phase 5 - RAMFS, VFS and initrd scaffolds implemented
- [~] Phase 6 - syscall ABI scaffold started
- [~] Device model - static device registry scaffold implemented
- [ ] Phase 7 - graphics foundation
- [ ] Phase 8 - UI stack
- [ ] Phase 9 - multi-architecture growth

## Security segment - high priority

- Primary security roadmap: `docs/security-roadmap.md`
- Current security foundations implemented:
  - [x] unified logger, serial diagnostics, `panic()` and `ASSERT`
  - [x] CPU exception reporting
  - [x] frame allocator and kernel heap scaffolds
  - [x] syscall ABI scaffold and user transition scaffold
  - [x] syscall argument validation scaffold
  - [x] allocator integrity self-check scaffold
  - [x] safe memory helper API scaffold
  - [x] boot module metadata validation scaffold
  - [x] ELF metadata validation scaffold
  - [x] `.tapp` package manifest contract, host validation, detached signing and install-gate verifier
- Near-term security priorities:
  - [ ] paging protection in runtime
  - [x] allocator misuse detection beyond current consistency checks
  - [~] module trust and validation policy
  - [ ] permissions and userspace isolation

## Guiding rules

1. Keep the kernel small and modular.
2. Separate CPU-specific code from platform-specific code early.
3. Prefer stable internal abstractions before expanding features.
4. Ship incremental milestones that remain bootable.
5. Build GUI only after timer, memory, scheduler, input and diagnostics are stable.

## Target layer split

- `boot/` - boot protocol entry code
- `arch/` - CPU-specific code (`i686`, `x86_64`, later `aarch64`)
- `platform/` - machine-specific code (`pc`, `qemu-aarch64`, `raspberry-pi`)
- `kernel/` - scheduler, memory, panic, interrupts, syscall, VFS
- `drivers/` - serial, input, timer, video, storage
- `libk/` - freestanding support library
- `api/` - stable internal and future syscall-facing APIs
- `ui/` - console, TUI, graphics, widgets, WM
- `apps/` - shell, test apps, future userland tools

## Phase plan

### Phase 0 - baseline hardening
- [x] add `serial` logging
- [x] add `panic()` and `assert`
- [x] centralize kernel logging
- [x] document architecture split and milestone plan

### Phase 1 - interrupt foundation
- [ ] add `GDT` cleanup if needed
- [x] add `IDT`
- [x] add exception handlers
- [x] add IRQ dispatch abstraction
- [x] prepare `arch_interrupt_init()`

### Phase 2 - time and input
- [x] add `PIT` timer driver
- [x] add keyboard IRQ path
- [x] introduce buffered input queue
- [x] prepare generic input events
- [x] add IRQ and PIT diagnostic shell commands
- [x] enable hardware IRQ0 in runtime
- [x] enable keyboard IRQ1 in runtime
- [x] keep polling fallback for missed keyboard input
- [x] add keyboard IRQ/input diagnostics

### Phase 3 - memory management
- [x] parse Multiboot memory map
- [x] add physical frame allocator
- [x] add `kmalloc`/`kfree`
- [ ] prepare paging and address-space abstractions

#### Ordered execution slices
1. [x] parse and expose Multiboot memory regions
2. [x] reserve kernel and boot structures
3. [x] implement bitmap frame allocator
4. [x] add page-sized allocation helpers
5. [x] add small kernel heap allocator
6. [x] prepare paging interfaces without enabling userspace yet
7. [x] add bootstrap page protection flag query and validation self-test
8. [x] connect address-space region rights to paging protection flags
9. [x] track boot modules as read-only kernel address-space regions

### Phase 4 - tasking
- [x] add task descriptor
- [x] add bootstrap and idle task scaffolds
- [x] initialize scheduler during boot
- [x] count scheduler ticks from PIT IRQ0
- [x] add `taskinfo` and `schedinfo` diagnostics
- [~] add idle task, `yield`, `sleep` scaffolds
- [x] add kernel stack ownership scaffold
- [x] add i686 context switch ABI scaffold
- [ ] implement active context switch
- [ ] implement round-robin scheduler

### Phase 5 - file and object layer
- [x] add `ramfs`
- [x] add `VFS` interfaces
- [x] add initrd/module loading skeleton
- [x] validate boot module metadata before ELF scanning
- [x] add TinyOS-native RAMFS browsing, viewing and simple edit tools in shell

### Phase 6 - syscall and userspace prep
- [x] define syscall ABI
- [x] add syscall argument validation scaffold
- [x] add language/runtime manifest for native and planned sandboxed app targets
- [x] add capability-gated application profile manifest
- [x] add launch-policy dry checks before real app execution exists
- [x] add `.tapp` app package registry, RAMFS metadata, host validation, detached signing and install-gate verifier
- [x] add user-facing system management tool manifest
- [x] add secure image/provisioning manifest for future signed, encrypted and remotely deployed images
- [x] add user/kernel transition stubs
- [x] add ELF loader skeleton
- [x] add strict ELF metadata validation scaffold
- [ ] move shell toward process-style architecture

### Device model groundwork
- [x] add static device registry scaffold
- [x] register boot console, serial diagnostics, PIC, PIT, input queue, keyboard and RAMFS
- [x] add `devices` shell diagnostic command
- [x] add registry lookup by device name and class
- [x] add RAM-backed block device class scaffold
- [x] add framebuffer/text-grid surface class scaffold
- [x] add `device`, `blockinfo` and `fbinfo` shell diagnostics
- [x] expose boot device metadata under RAMFS `/devices`
- [x] connect RAM block device to a read-only VFS storage path under `/volumes`
- [x] add `storageinfo` diagnostics for block VFS mount state
- [ ] add write-through block file experiments behind strict validation
- [ ] add linear framebuffer mode through bootloader metadata

### Phase 7 - graphics foundation
- [x] add framebuffer/text-grid surface abstraction
- [x] add text-grid renderer scaffold
- [x] add text-grid fill/clear renderer primitives
- [x] add `renderinfo`, `rendertest` and `renderfilltest` shell diagnostics
- [x] add terminal UI scaffold over renderer
- [x] add terminal clear/panel helpers over renderer primitives
- [x] add `terminalinfo`, `terminaltest`, `terminalclear` and `terminalpaneltest` shell diagnostics
- [x] add first TUI widget scaffold with label and button drawing
- [x] add UI event dispatch bridge for TUI widgets
- [x] add `widgetinfo`, `widgettest`, `widgetdispatch` and `widgetactiontest` shell diagnostics
- [x] add UI event queue scaffold over the generic input queue
- [x] add `uieventinfo`, `uieventpump`, `uieventpeek` and `uieventtest` shell diagnostics
- [~] add 2D software renderer primitives
- [x] add event queue
- add text rendering on framebuffer

### Runtime requirements groundwork
- [x] add kernel-visible minimum requirements manifest
- [x] add `requirements` shell diagnostic command
- [x] add RAMFS `/system/requirements.txt`
- [x] add `make test-minimal` for the 32 MiB QEMU boot envelope

### Phase 8 - UI stack
- [x] add first terminal UI state scaffold
- [x] add UI-facing event queue
- [x] add first TUI widgets
- add simple window manager
- add reusable GUI widgets
- add desktop shell prototype

### Phase 9 - multi-architecture growth
- keep `i686` as reference target
- add `x86_64` CPU backend
- add `platform/qemu-x86_64`
- add `aarch64` through `QEMU virt`
- defer physical boards until emulator targets are stable

## Phase dependencies

- no scheduler before timer
- no userspace before paging and syscall ABI
- no GUI before allocator, events and framebuffer
- no cross-platform expansion before `arch/` and `platform/` contracts are stable

## Immediate implementation order

1. diagnostics
2. interrupts
3. timer
4. keyboard IRQ path
5. memory map and allocator
6. kernel heap
7. tasking
8. VFS skeleton
9. syscall skeleton
10. framebuffer and event queue

## Sequential rollout plan

### Milestone A - stable kernel base
- keep boot stable after every change
- finish safe exception handling
- stabilize hardware IRQs one source at a time
- keep `irqinfo` and `timerinfo` usable before and after enabling runtime IRQs

### Milestone B - kernel memory
- parse bootloader memory map
- implement physical memory accounting
- add frame allocator and heap primitives

### Milestone C - execution model
- add task descriptors
- add scheduler
- add timing primitives

### Milestone D - kernel objects and files
- introduce `ramfs`
- introduce `VFS`
- prepare initrd loading

### Milestone E - syscall boundary
- define syscall ABI
- add user entry path
- add executable loader skeleton

### Milestone F - visual stack
- add framebuffer abstraction
- add event queue
- add software 2D primitives
- add TUI then GUI layers

### Milestone G - portability
- add `x86_64`
- add `qemu-x86_64`
- add `aarch64` emulator target

## Near-term deliverables

### v0.2
- serial log
- panic/assert
- kernel logger
- shell test command for panic path

### v0.3
- IDT skeleton
- exception entry points
- timer init scaffold
- IRQ-ready keyboard design

### v0.4
- memory map parser
- frame allocator
- kernel heap

### v0.5
- scheduler and kernel tasks

### v0.6
- VFS and initrd skeleton

### v0.7
- syscall and ELF loader skeleton

### v0.8
- framebuffer renderer and event queue

### v0.9
- x86_64 backend bootstrap

### v1.0
- stable demo system with userland shell and minimal GUI stack
