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

After relocating the repository, old CMake caches may still refer to absolute
paths in the previous location. Run these commands from the new repository:

```powershell
python tools/scenectl.py build --preset dev --fresh
python tools/scenectl.py build --preset release --fresh
```

`--fresh` uses CMake's cache regeneration to recreate `CMakeCache.txt` and
`CMakeFiles/` for the selected preset, then configures and builds. It does not
clean `.scene-sdk/` or historical logs. `configure` accepts this option too.
Manually cached settings are reset; the current preset and command arguments
determine the new configuration. Ordinary builds do not need `--fresh`.

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

Run `python tools/scenectl.py smoke --mouse --timeout 10` for the matching
pointer test. It sends relative motion through QEMU Monitor and requires an
IRQ12-delivered pointer event. Neither input self-test changes the normal
interactive build configuration permanently; the next configure selects the
requested options explicitly.

Run `python tools/scenectl.py smoke --text-input --timeout 15` for text
translation. It checks US layout/UTF-8 boundaries, injects key groups, and
requires `aAAa!b Z` with no queue loss. Add `--pic`, `--tsc-clock`, or
`--preset release` for compatibility/optimized paths. `--text-input` is mutually
exclusive with other focused self-tests; `SCENEOS_TEXT_INPUT_SELF_TEST` defaults
to OFF. Input injection follows serial readiness markers and cleans up its
QEMU process on faults or failed waits. A normal `build` or `smoke` disables all
self-tests; direct `cmake --build` retains the cached configuration. See
[input core and PS/2](input-and-ps2.md).

`python tools/scenectl.py smoke --clock-check --timeout 10` validates CPUID
frequency decoding and diagnostic boundary cases. Add `--tsc-clock` to test
the TSC fallback with these checks. Actual CPU diagnostics and synthetic edge
cases are reported separately; see [interrupts and time](interrupts-and-time.md).

Optional Local APIC timer acceptance:

```powershell
python tools/scenectl.py smoke --lapic-timer --timeout 10
python tools/scenectl.py smoke --lapic-timer --tsc-clock --timeout 10
python tools/scenectl.py smoke --lapic-timer --pic --timeout 10
python tools/scenectl.py smoke --preset release --lapic-timer --timeout 10
python tools/scenectl.py smoke --keyboard --pic --timeout 10
python tools/scenectl.py smoke --mouse --pic --timeout 10
```

The first command calibrates against HPET. `--tsc-clock` selects PIT calibration
with the TSC monotonic source. `--pic` forces the 8259A path: the local timer
must report unavailable while PIT/input still work. That flag can combine with
input or clock modes; `--lapic-timer` is exclusive with other focused self-tests.
`SCENEOS_LOCAL_TIMER_SELF_TEST` and `SCENEOS_FORCE_PIC` both default to OFF.
scenectl explicitly sets every test/force option on each configuration. A
normal `build` or `smoke` restores defaults after testing; direct `cmake --build`
retains cached options. Any `[PANIC]` makes smoke fail. CI configuration covers
HPET/PIT local-timer checks, PIC fallback, and input compatibility paths.
