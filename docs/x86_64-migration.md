# TinyOS x86_64 migration

This document defines how TinyOS moves from the `i686` reference kernel to a
64-bit `x86_64` target, and what must be true in the current tree before each
step starts.

## Why the migration is required

The 32-bit kernel cannot satisfy the product requirements:

- **Memory.** `kernel/memory/frame_allocator.cpp` caps management at
  `MaxFrames = 64 * 1024` frames and, since Gate 0, at
  `paging::BootstrapIdentityLimit` (16 MiB). Everything above the bootstrap
  identity window is unreachable because the kernel has no direct physical map.
  A 32-bit kernel with this design cannot use large amounts of RAM.
- **Address space.** There is a single bootstrap page directory shared by kernel
  and the embedded ring-3 `init`. Without per-process tables there is no memory
  isolation.
- **Protection.** `i686` without PAE has no NX bit, so W^X cannot be enforced.
- **Virtualization.** VT-x/AMD-V with EPT and an IOMMU require long mode.

## Target contract

| Layer | Owns | Must not own |
|-------|------|--------------|
| `arch/x86_64/` | GDT/IDT/TSS, paging structures, context switch, MSR/CPUID, FPU state, syscall entry | device knowledge, filesystem, scheduling policy |
| `platform/pc/` | ACPI parsing, PCI enumeration, timer selection, interrupt controller wiring | CPU mode switching |
| `kernel/` | scheduler, memory allocators, VFS, syscalls, process table | direct port I/O, MSR access |
| `drivers/` | one driver per device, registered through the driver model | boot policy |
| `libk/` | freestanding helpers (`mem*`, `str*`, formatting) | kernel state |

The generic kernel must compile against an `arch` interface and never include
`arch/x86_64/*.hpp` directly.

## Boot path

1. GRUB loads an ELF64 image through Multiboot2.
2. A 32-bit entry stub validates the handoff, enables PAE, sets up an initial
   PML4 with an identity-mapped low 1 GiB using 2 MiB pages, sets `EFER.LME`
   and `CR0.PG`, and far-jumps into the 64-bit entry point.
3. The 64-bit entry switches to a kernel stack in the higher half, reloads CS/SS
   with 64-bit selectors, and calls `kernel_main`.

The linker script places the kernel at `0xFFFFFFFF80000000`; the physical load
address stays at 1 MiB. Nothing else may assume identity mapping after
`arch::initialize()` completes.

## Work breakdown

### A1 - build matrix

- `ARCH=i686|x86_64` in `Makefile`, with per-arch `CXXFLAGS`, linker script and
  QEMU binary (`qemu-system-i386` / `qemu-system-x86_64`).
- Independent `OBJ_DIR` and `ISO` per architecture so the two builds never share
  stale objects. The current tree already hit this class of bug: changing
  `CXXFLAGS` does not invalidate existing `.o` files.
- `tests/boot-markers.txt` stays shared; architecture-specific markers live in
  `tests/boot-markers-<arch>.txt`.

### A2 - CPU bringup

- `arch/x86_64/gdt.cpp`: 64-bit GDT plus a TSS with IST stacks for double fault,
  NMI and machine check. A double fault on a broken stack must not triple-fault.
- `arch/x86_64/idt.cpp`: 256 gates, exception stubs passing the full frame
  (`vector`, `error`, `rip`, `cs`, `rflags`, `rsp`, `ss`) to the C++ handler.
- `arch/x86_64/cpu.cpp`: CPUID (vendor, family, features, topology), MSR
  read/write, CR0/CR4 (`PAE`, `PGE`, `OSFXSR`, `OSXMMEXCPT`, `SMEP`, `SMAP`,
  `FSGSBASE`), `EFER` (`LME`, `NXE`, `SCE`).
- `arch/x86_64/fpu.cpp`: enable x87/SSE, save/restore `FXSAVE` area in the
  context switch. Without this any userland using floating point is silently
  wrong.

### A3 - paging and memory

- 4-level paging, kernel mapped in PML4 slot 511.
- Direct physical map: a linear window over all RAM using 2 MiB and 1 GiB pages.
  This replaces the bootstrap identity map discussed in Phase B.
- Per-address-space PML4 with a shared kernel half.
- `copy_from_user`/`copy_to_user` walk page tables.

### A4 - SMP

- ACPI MADT for LAPIC/IOAPIC discovery, LAPIC timer as the preemption source.
- Trampoline bring-up for application processors (INIT-SIPI-SIPI).
- Per-CPU state through `GSBASE`.

`i686` stays single-core; SMP is x86_64-only to avoid maintaining two interrupt
controller paths.

## Non-goals until the port is stable

- Dynamic kernel modules.
- Loadable filesystems.
- A desktop or any graphical surface.
- Sandbox runtimes.

## Verification gates

Each step must leave `make ARCH=<arch> test-boot` green.

| Step | Gate |
|------|------|
| A1 | Both architectures build; i686 still boots with all markers in `tests/boot-markers.txt` |
| A2 | x86_64 reaches a serial "long mode active" marker, exception handlers report full frames, double fault uses its IST stack |
| A3 | Boot with `-m 8G` reports the full usable pool; allocating and freeing 100k pages keeps `accounting_valid()` true |
| A4 | `-smp 4` brings up all CPUs, `make test-stability` shows no watchdog warnings or deadlocks |

`make test-boot-x86_64` verifies the A1/A2 marker contract from
`tests/boot-markers-x86_64.txt` and runs as part of `make test-gate`.

## Current state

`i686` remains the reference kernel and still passes the full marker contract in
`tests/boot-markers.txt`. The `x86_64` tree exists and boots to a verifiable
serial contract, but it is a bring-up path, not yet a kernel.

### What works on x86_64 today

`make ARCH=x86_64 iso` builds and `make test-boot-x86_64` boots it in QEMU:

- a 32-bit entry stub builds PML4/PDPT/PD, enables PAE, sets `EFER.LME`, turns on
  paging and far-jumps into 64-bit mode;
- the first 1 GiB is identity mapped with 2 MiB pages;
- CPUID probes vendor, family/model/stepping, logical processor count and the
  feature set the plan depends on (NX, APIC, x2APIC, MSR, FXSAVE, SSE/SSE2,
  SMEP, SMAP, FSGSBASE, RDTSC, 1 GiB pages, INVPCID, PCID, VMX, SVM);
- `CR4` bits are enabled only for features the CPU actually reports, and
  `EFER.NXE` is set only when NX exists;
- a 64-bit GDT with a TSS and three IST stacks (double fault, NMI, machine
  check);
- a 256-entry IDT where every vector has its own stub, error-code vectors push
  their own code, and fault reports include `rip`, `cs`, `rflags`, `rsp` and
  `cr2` for page faults;
- the Multiboot2 tag list is walked and total memory is reported.

Verified in QEMU on an AMD host: long mode entered, NX available, SVM present,
127 MiB reported from the Multiboot2 basic memory tag.

### What is still missing on x86_64

- No scheduler, no interrupts enabled, no allocation, no devices. The bootstrap
  halts deliberately after reporting.
- No higher-half mapping and no direct physical map; the kernel runs from the
  identity map the boot stub builds.
- The portable kernel sources are not compiled for x86_64 yet. The `Makefile`
  keeps a separate source list so a missing 64-bit backend fails loudly instead
  of linking a 32-bit assumption into the 64-bit kernel.
- No SMP, no ACPI, no IOAPIC.
