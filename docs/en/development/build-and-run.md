# Build, run, and bootstrap

## Host tools

- Python 3.10+
- CMake 3.25+
- Ninja
- Clang and LLD
- NASM
- QEMU, required
- Bochs, optional
- Release ISO: xorriso on Linux/macOS or in CI

Diagnose the host:

```text
python tools/scenectl.py doctor
```

Tools outside `PATH` can be selected with `SCENEOS_CMAKE`,
`SCENEOS_NINJA`, `SCENEOS_CLANG`, `SCENEOS_LLD`, `SCENEOS_NASM`,
`SCENEOS_XORRISO`, `SCENEOS_QEMU`, and `SCENEOS_BOCHS`. Each value is an
absolute executable path. On Windows, standard WinGet Links and installation
locations for CMake, LLVM, and QEMU are also detected automatically.

## Project-local bootstrap

```powershell
.\bootstrap.ps1
```

```sh
./bootstrap.sh
```

The current bootstrap downloads only a pinned Limine binary package to
`.scene-sdk/`, verifying its size and SHA-256. It does not install or modify
other host tools, need administrator access, edit the registry or global
`PATH`, update shell profiles, or write system directories.

## Build and run

```powershell
.\build.ps1
.\run.ps1
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
.\run.ps1 --emulator bochs
```

```sh
./build.sh
./run.sh
python3 tools/scenectl.py smoke
./run.sh --emulator bochs
```

The lower-level interface remains available:

```text
cmake --preset dev
cmake --build --preset dev
python tools/scenectl.py image
python tools/scenectl.py run
```

On Windows, `run` automatically uses QEMU with EDK2 and UEFI virtual FAT
media, preserving one-command execution. `image` uses xorriso to create a
distributable ISO and is intended for Linux/macOS or CI. Bochs currently still
requires an ISO.

SceneOS does not use oscdimg to create its media. The official executable
states that it is licensed only for producing Microsoft-authorized content,
which SceneOS must not assume. `doctor` reports and ignores oscdimg when found.

All generated output stays under `build/<preset>/`. The build does not copy
into NNOS, overwrite source files, or write emulator installation folders.

GitHub Actions installs tools in an ephemeral Linux runner, builds the ISO, and
checks boot progress through the QEMU serial console. This CI environment is
isolated from physical development machines.

## Acceptance signal

The QEMU serial console should report `SceneOS kernel bootstrap`, GDT/TSS/IDT,
bootloader, memory-map, `Local APIC + IOAPIC`, IRQ registration, PIT GSI,
hardware ticks, the HPET/TSC clock source, timer-queue verification, first
physical page, PS/2 keyboard readiness, and framebuffer information. The
display should show a dark blue desktop, taskbar, and validation window.

Run `python tools/scenectl.py smoke --keyboard --timeout 10` for the automated
input acceptance test. The runner waits for the kernel, sends `A` through the
QEMU monitor, and requires an IRQ1-delivered semantic key event with no queue
loss. This mode is headless and does not type into the host desktop.
