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
nanosecond monotonic time. Both clock paths record the PIT-measured TSC
frequency, CPUID.15H frequency, and invariant flag, comparing frequencies with
a 5% tolerance. CPUID data is diagnostic only and does not override source
selection or the measured frequency. A fixed 32-entry min-heap timer queue supports
scheduling, cancellation, and due-callback dispatch outside IRQ context.
The core IRQ layer now owns a fixed 16-entry legacy-line registry. The x86
layer routes each attached line through its MADT override to the IOAPIC; PIT
also attaches IRQ0 through this interface instead of being hard-coded in the
interrupt dispatcher. An optional BSP Local APIC timer uses dedicated vector
48, calibrates against HPET or PIT, and supports one-shot/periodic delivery,
stop, and pending-interrupt drain without consuming legacy IRQ slots. Normal
boot still uses PIT. PIC fallback disables any firmware-enabled Local APIC to
restore direct 8259A delivery. Cross-CPU clock synchronization, local-timer
scheduling/wakeup integration, dynamic queue capacity, and shared IRQ handlers
remain pending. The
input core adds a hardware-neutral semantic key-event queue. The initial i8042
driver configures PS/2 translation, consumes Set 1 bytes on IRQ1, tracks key
state and modifiers, and publishes press/release/repeat events. Above the device
layer, a stateless consumer-side module applies a minimal US layout, retains
raw key events and source sequence numbers, handles Shift/Caps, repeat, and
Ctrl/Alt suppression, and encodes Unicode scalars as UTF-8. It generates no
characters inside IRQ handlers and never writes text back into the raw input
queue. More layouts, dead keys, input methods, and composition remain pending.
A shared i8042 controller layer also
enables the second port. The mouse probes IntelliMouse during boot and consumes
three/four-byte packets on IRQ12 according to the actual ID, publishing nine-bit
relative motion, three-button changes, and positive-up scrolling. Protocol
decoding can be tested independently; command polling is boot-only with IF
clear. Five buttons, horizontal scrolling, absolute input, and multi-consumer
routing remain unsupported.

The first M3 stage supplies cooperative BSP threads sharing the kernel address
space: one permanent boot context and eight workers, READY/RUNNING/BLOCKED/EXITED
states, explicit round-robin yield, and reaping after exit. Each worker maps
a 32 KiB NX stack between unmapped guard pages in a region starting at
`0xffffc10000000000`. Core scheduling uses an architecture continuation
interface with interrupts disabled during state changes and switches; the
resumed thread restores interrupts. Wait queues check conditions and enqueue
under one interrupt-disabled interval, with FIFO single/batch wakeup and
condition rechecks. Input IRQs mark blocked consumers READY; ordinary context
later yields to them while bootstrap stays runnable. Normal boot has no
background workers and PIT does not preempt. Wait timeouts/cancellation, an
idle loop, private address spaces, FPU/SIMD, TLS, SMP, and Ring 3 remain future work.
