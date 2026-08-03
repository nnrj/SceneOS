# System architecture

## Layers

```text
Applications and desktop
  ├─ Windows-style desktop shell, file manager, and settings
  ├─ Unix-style terminal and command-line tools
  └─ Applications

User-space system services
  ├─ init and service management
  ├─ window server and compositor
  ├─ aid and the JSON operation gateway
  └─ migratable network and storage services

System ABI
  ├─ ELF64 loading
  ├─ system calls
  ├─ IPC, handles, permissions, and auditing
  └─ libc and the Scene SDK

Modular monolithic kernel
  ├─ scheduling, processes, and threads
  ├─ physical memory, page tables, and virtual memory
  ├─ VFS, block devices, and core drivers
  └─ interrupts, timers, PCI, and ACPI

Architecture and platform
  └─ arch/x86_64, with arch/aarch64 possible later
```

## GUI and headless operation

The kernel does not depend on a GUI. SceneOS defines two supported boot forms:

- `desktop`: the default product mode, starting the compositor, desktop shell,
  and system applications.
- `console`: recovery, debugging, server, and display-less operation.

The desktop, command line, and kernel each have independent milestones and
release gates. Headless support does not permit the GUI to become indefinitely
optional work.

## Architecture boundary

Core code must not directly use CR3, port I/O, APIC registers, or x86 page table
formats. `arch/<architecture>` owns interrupt control, CPU-local state, page
tables and TLB operations, context switching, atomics and barriers, and
architecture timers. These interfaces are selected at compile time and do not
require runtime virtual dispatch.

## Monolithic to hybrid evolution

Initial modules use function calls. Interfaces should still define request
objects, error reporting, lifecycle, ownership, timeout, and cancellation
semantics without exposing arbitrary internal pointers. Selected modules may
later move behind IPC. SceneOS does not promise an effortless conversion to a
pure microkernel; isolation boundaries will follow reliability and performance
evidence.

## Current implementation status

M1 now installs a SceneOS-owned x86-64 GDT, TSS, and exception IDT. Double
fault, NMI, and machine check use independent IST stacks; a diagnostic
exception can also verify a real stack switch through a dedicated IST stack.
The boot adapter
converts the Limine memory map into architecture-neutral usable physical
regions; the core memory module provides early 4 KiB page allocation while
reserving the first 1 MiB. The physical allocator uses per-region bitmaps and
supports release. The paging module clones Limine's active four-level
structures and switches to a SceneOS-owned CR3; it supports 4 KiB mapping,
unmapping, and physical-page return. The kernel heap starts at
`0xffffc00000000000`, commits physical pages on demand, and supports block
splitting, adjacent-free-block coalescing, freeing, and reuse. Intermediate
page-table reclamation, returning unused heap-tail pages, concurrent allocation,
slab/object caches, and per-thread TSS RSP0 updates are not implemented yet.
M2 now performs 8259A-compatible initialization, discovers the Local APIC,
IOAPIC, and IRQ overrides from ACPI MADT, and routes the 100 Hz PIT IRQ to the
BSP Local APIC on the default Q35 path. A shared ACPI lookup layer validates
RSDP, XSDT/RSDT, and target tables. The core time layer prefers the 64-bit HPET
main counter and falls back to a PIT-calibrated TSC, exposing either as
nanosecond monotonic time. A fixed 32-entry min-heap timer queue supports
scheduling, cancellation, and due-callback dispatch outside IRQ context.
Cross-CPU clock synchronization, Local APIC timer support, dynamic queue
capacity, and general driver IRQ registration remain pending.
