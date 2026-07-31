# SceneOS

SceneOS is an experimental x86-64 desktop operating system. Its native
environment combines Unix-style command-line semantics, a first-class
Windows-style desktop experience, and a structured automation interface for
AI agents.

The project is in its bootstrap stage. The current kernel starts through the
Limine boot protocol, initializes a serial debug console, reports boot
information, installs its own GDT/TSS/IDT and CR3, manages physical pages with
per-region bitmaps, maps and unmaps 4 KiB pages, provides a reusable
higher-half kernel heap, discovers APIC topology through ACPI MADT, routes a
PIT hardware timer through Local APIC/IOAPIC, and draws a simple framebuffer
scene.

## Quick start

Requirements: Python 3.10+, CMake, Ninja, Clang/LLD, NASM, and QEMU.
ISO production uses xorriso on Linux/macOS or in CI. QEMU on Windows boots
directly from project files without an ISO tool.

On Windows:

```powershell
.\bootstrap.ps1
.\build.ps1
.\run.ps1
```

On Linux:

```sh
./bootstrap.sh
./build.sh
./run.sh
```

`bootstrap` downloads only the pinned Limine binary package into
`.scene-sdk/`. It does not require administrator privileges and does not
modify the registry, global `PATH`, shell profiles, or system directories.
Run `python tools/scenectl.py doctor` to see missing host tools before
building.

Documentation is available in [Chinese](docs/zh/README.md) and
[English](docs/en/README.md).

## Current scope

- Architecture: x86-64 only
- Language: C17 and NASM
- Kernel model: modular monolithic, with isolation-friendly interfaces
- Build: CMake presets with Ninja
- Emulator: QEMU by default; Bochs is optional
- Boot: pinned Limine protocol and binary release
- License: MIT

SceneOS is a clean implementation. NNOS is used as a historical and behavioral
reference, not as the source tree of the new kernel.

See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md).
