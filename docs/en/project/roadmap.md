# Implementation roadmap

This roadmap defines order, not dates. Every milestone requires automated,
observable completion criteria.

## M0: reproducible bootstrap (complete)

- C17 and NASM x86-64 freestanding project.
- CMake Presets and Ninja.
- Pinned Limine BIOS/UEFI boot image.
- Serial logging, panic, boot information, and memory-map summary.
- Framebuffer boot scene with a headless fallback.
- Windows/Linux wrappers, tool diagnosis, and project-local bootstrap.
- QEMU UEFI virtual-FAT smoke test; Windows can run without an ISO tool.

## M1: exceptions and memory (in progress)

- GDT, IDT, exception entry, and register frames. (initial version complete)
- Per-region bitmap physical allocation, release, and reuse. (initial version
  complete)
- SceneOS-owned CR3, four-level map/unmap, and higher-half heap. (initial
  version complete)
- Page-fault diagnostics, address translation, and multi-page heap tests.
  (initial version complete)
- TSS, Ring 0 stack, and critical-exception IST stacks. (initial version
  complete)
- Splitting, coalescing, releasing, and reusable kernel heap. (initial version
  complete)
- Intermediate-table reclamation, reservation tracking, contiguous
  allocation, heap concurrency, and slab allocation.

## M2: time, interrupts, and input (in progress)

- PIC-compatible bootstrap, ACPI MADT discovery, and Local APIC/IOAPIC
  migration. (initial version complete)
- PIT 100 Hz hardware ticks with an interrupt watchdog. (initial version
  complete)
- Shared ACPI table lookup, HPET as the preferred clock, a PIT-calibrated TSC
  fallback, nanosecond conversion, and a fixed-capacity timer queue. (initial
  version complete)
- Fixed legacy-IRQ registration, MADT-override routing, attach/detach, and PIT
  driver migration. (initial version complete)
- CPUID/TSC frequency cross-checking, Local APIC timer evaluation, and dynamic
  timer capacity.
- PS/2 keyboard and mouse with a unified input event model.

## M3: threads, processes, and user space

- Software context switching, preemption, and wait queues.
- Ring 3, per-process address spaces, system calls, and IPC.
- ELF64 user programs, initramfs, and a small libc.

## M4: storage and Unix command line

- Block layer, ATA PIO validation driver, then AHCI or virtio.
- VFS, FAT32 read/write, file descriptors, and native path semantics.
- Shell, pipelines, redirection, and foundational Unix tools.

## M5: first-class desktop

- User-space window server, 32-bit ARGB compositor, and window protocol.
- Unicode fonts, input methods, clipboard, drag-and-drop, DPI, and themes.
- Taskbar, start surface, file manager, settings, and terminal.

## M6 and beyond

- PCI, ACPI, SMP, USB, audio, and modern storage.
- Network stack with virtio-net or e1000.
- AI service, structured operations, permissions, and auditing.
- Dynamic linking, application bundles, and side-by-side runtimes.
- Stable ABI, self-hosting toolchain, and continuous legacy compatibility.
- GPU acceleration, 3D composition, and a VR shell.
