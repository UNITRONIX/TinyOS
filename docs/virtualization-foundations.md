# TinyOS virtualization foundations

The goal is for TinyOS to run virtual machines with near-native performance,
including PCI passthrough and eventually GPU passthrough. This document defines
the prerequisites, the execution model and the staged plan.

## Prerequisites (gate H1)

VT-x or AMD-V cannot be bolted onto the current kernel. Each item below is a
hard requirement, because a hypervisor built on a missing foundation produces
silent memory corruption instead of a clean failure:

| Requirement | Why it blocks virtualization |
|-------------|------------------------------|
| `x86_64` long mode | VT-x with EPT requires 64-bit; see `docs/x86_64-migration.md` |
| Per-process address spaces | Each guest owns a nested page hierarchy; without isolation a guest can map host memory |
| x86_64 SMP with per-CPU state | Each vCPU is scheduled as a kernel task; per-CPU state is how VM exits find their VM |
| Spinlocks and wait queues | VM exits run in interrupt context; unsynchronized host structures corrupt under load |
| PCI enumeration with BAR allocation | Passthrough requires assigning and remapping real BARs |
| MSI/MSI-X | Modern devices, GPUs included, are MSI-only in practice |
| IOMMU (VT-d / AMD-Vi) | The only thing that makes device passthrough safe |
| Hugepages (2 MiB / 1 GiB) | Guest RAM backed by tiny pages destroys TLB performance |
| DMA API | Drivers must map buffers through IOMMU domains, not raw physical addresses |
| Task pinning and CPU affinity | vCPU threads must not migrate mid-exit |

## Execution model

A vCPU is an ordinary kernel task with a saved `VMCS`/`VMCB` pointer, a guest
register frame, an address space for its nested paging tables, and an assigned
host CPU it is pinned to. A VM is a container of vCPUs plus a list of assigned
devices and a memory backing store.

This model is what makes the design fast and simple:

- guest execution is a normal context switch into a task, so the existing
  scheduler does load balancing for free;
- a VM exit returns to a normal kernel handler, so a misbehaving guest cannot
  corrupt host state more than a normal syscall could;
- guest RAM is regular kernel-allocated memory described to EPT, so the memory
  accounting a later memory phase builds applies to guests unchanged.

## Stages

### H2 - CPU virtualization

- Detect VMX/SVM through CPUID; refuse to load the hypervisor on hardware
  without it rather than partially working.
- `VMXON` per logical CPU, `VMCS` setup, EPT with 2 MiB and 1 GiB entries.
- Handle the exits a minimal guest needs: `CPUID`, port I/O, EPT violation,
  `HLT`, `INVLPG`, `MSR` access.
- Deliver guest interrupts through the host LAPIC.

Exit criterion: a single-vCPU guest boots to a serial marker and shuts down.

### H3 - performance

Near-native performance is not automatic; each of these items removes a
measurable cost:

- VPID (Intel) / ASID (AMD) so a VM exit does not flush the whole TLB;
- APICv with posted interrupts so device interrupts reach the guest without a
  host exit;
- MSR bitmaps so guests read and write common MSRs without exiting;
- TSC offsetting and a preemption timer for cheap time virtualization;
- zero-copy virtqueues (a vhost-style model) so packet and block traffic does
  not bounce through host buffers;
- vCPU pinning with cache-aware placement.

### H4 - IOMMU and passthrough

- Parse DMAR (Intel) / IVRS (AMD) from ACPI.
- Model IOMMU domains; every DMA-capable device is bound to exactly one domain.
- Interrupt remapping so MSI/MSI-X interrupts can target a guest safely.
- PCIe ACS checks: devices in the same IOMMU group cannot be split across guests.
- Assign a PCI function to a guest, including BAR and option ROM mapping and
  MSI-X table remapping.
- Function-level reset and power management transitions on assignment and
  release; a GPU that is not reset properly hands stale state to the next guest.

Exit criterion: a guest receives a passed-through NIC and moves traffic.

### H5 - GPU passthrough

GPU passthrough is the last step because it depends on everything above being
correct and adds hardware-specific problems:

- map large BARs (GPUs commonly expose multi-gigabyte BARs; resizeable BAR
  support or a 64-bit MMIO window may be required);
- legacy VGA arbitration and decode, so the host does not lose its own console;
- PCIe power states and ASPM interaction;
- the host must keep no driver bound to the device, while the guest loads its
  own;
- reliable reset across vendor families; some GPUs require vendor-specific
  quirks and some cannot be reset at all without a PCIe link reset.

Exit criterion: a guest drives a display through the passed-through GPU under
sustained load while the host remains responsive.

## What this rules out

- **Paravirtualized-only designs** are not the goal: they cannot deliver GPU
  passthrough. VirtIO remains valuable for I/O that does not need to be native.
- **Nested virtualization inside a guest** is not a requirement and must not
  shape the design.
- **Live migration and snapshots** come after passthrough stability; they need
  dirty-page tracking and a device state model that does not exist yet.

## Sequencing note

Nothing in the hypervisor plan is scheduled until gate H1 passes in full.
Building VM exit handling on top of an allocator that caps out at 16 MiB
reachable memory, or on a scheduler without synchronization, produces a
hypervisor that fails under load in ways that are extremely hard to diagnose.
