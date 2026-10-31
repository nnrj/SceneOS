#!/usr/bin/env python3
"""SceneOS build orchestration.

This tool deliberately avoids system-wide changes. Downloads and generated
files are confined to .scene-sdk/ and build/ under the repository root.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = ROOT / "build"
SDK_ROOT = ROOT / ".scene-sdk"
LOCK_FILE = ROOT / "tools" / "toolchain.lock.json"

HOST_TOOLS = {
    "cmake": ("SCENEOS_CMAKE", ("cmake", "cmake.exe")),
    "ninja": ("SCENEOS_NINJA", ("ninja", "ninja.exe")),
    "clang": ("SCENEOS_CLANG", ("clang", "clang.exe")),
    "lld": ("SCENEOS_LLD", ("ld.lld", "ld.lld.exe")),
    "nasm": ("SCENEOS_NASM", ("nasm", "nasm.exe")),
    "xorriso": ("SCENEOS_XORRISO", ("xorriso", "xorriso.exe")),
    "oscdimg": ("SCENEOS_OSCDIMG", ("oscdimg", "oscdimg.exe")),
    "qemu": (
        "SCENEOS_QEMU",
        ("qemu-system-x86_64", "qemu-system-x86_64.exe"),
    ),
    "bochs": ("SCENEOS_BOCHS", ("bochs", "bochs.exe")),
}

WINDOWS_TOOL_HINTS = {
    "cmake": (Path("C:/Program Files/CMake/bin/cmake.exe"),),
    "clang": (Path("C:/Program Files/LLVM/bin/clang.exe"),),
    "lld": (Path("C:/Program Files/LLVM/bin/ld.lld.exe"),),
    "qemu": (Path("C:/Program Files/qemu/qemu-system-x86_64.exe"),),
    "oscdimg": (
        Path(
            "C:/Program Files (x86)/Windows Kits/10/"
            "Assessment and Deployment Kit/Deployment Tools/"
            "amd64/Oscdimg/oscdimg.exe"
        ),
        Path(
            "C:/Program Files (x86)/Windows Kits/10/"
            "Assessment and Deployment Kit/Deployment Tools/"
            "x86/Oscdimg/oscdimg.exe"
        ),
    ),
}


class SceneCtlError(RuntimeError):
    pass


def load_lock() -> dict:
    with LOCK_FILE.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def find_tool(name: str) -> str | None:
    environment_name, candidates = HOST_TOOLS[name]
    configured = os.environ.get(environment_name)
    if configured:
        path = Path(configured).expanduser().resolve()
        if path.is_file():
            return str(path)

    for candidate in candidates:
        result = shutil.which(candidate)
        if result:
            return result

    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if local_app_data:
            winget_link = Path(local_app_data) / "Microsoft/WinGet/Links"
            for candidate in candidates:
                path = winget_link / candidate
                if path.is_file():
                    return str(path.resolve())
        for path in WINDOWS_TOOL_HINTS.get(name, ()):
            if path.is_file():
                return str(path.resolve())
    return None


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    environment: dict[str, str] | None = None,
) -> None:
    print("+", " ".join(command))
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        env=environment,
    )
    if completed.returncode != 0:
        raise SceneCtlError(
            f"command failed with exit code {completed.returncode}: {command[0]}"
        )


def version_line(executable: str) -> str:
    for argument in ("--version", "-version"):
        try:
            completed = subprocess.run(
                [executable, argument],
                check=False,
                capture_output=True,
                text=True,
                timeout=5,
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        output = (completed.stdout or completed.stderr).strip()
        if output:
            return output.splitlines()[0]
    return "version unavailable"


def command_doctor(_: argparse.Namespace) -> None:
    required = ("cmake", "ninja", "clang", "lld", "nasm", "qemu")
    image_tools = ("xorriso",)
    optional = ("bochs",) + image_tools
    missing: list[str] = []

    print("SceneOS host tool diagnosis")
    print(f"Repository: {ROOT}")
    print(f"Python: {sys.executable} ({sys.version.split()[0]})")

    for name in required + optional:
        executable = find_tool(name)
        status = "required" if name in required else "optional"
        if executable:
            print(f"[ok]      {name:8} {executable}")
            print(f"          {version_line(executable)}")
        else:
            print(f"[missing] {name:8} ({status})")
            if name in required:
                missing.append(name)

    if not any(find_tool(name) for name in image_tools):
        expected = " or ".join(image_tools)
        if os.name == "nt":
            print(
                f"[optional] image tool ({expected}); "
                "QEMU UEFI directory media remains available"
            )
        else:
            print(f"[missing] image tool ({expected})")
            missing.append(expected)

    if os.name == "nt" and find_tool("oscdimg") is not None:
        print(
            "[ignored] oscdimg  installed, but its executable restricts use "
            "to Microsoft-authorized content"
        )

    lock = load_lock()
    version = lock["limine_binary"]["version"]
    limine_root = SDK_ROOT / "limine" / version
    if (limine_root / ".sceneos-install.json").is_file():
        print(f"[ok]      limine   {limine_root}")
    else:
        print("[missing] limine   run bootstrap.ps1 or ./bootstrap.sh")
        missing.append("limine")

    if missing:
        raise SceneCtlError("missing required tools: " + ", ".join(missing))


def confirm_download(yes: bool, version: str, size: int) -> bool:
    print("Bootstrap plan:")
    print(f"  Download Limine {version} ({size} bytes)")
    print(f"  Install only under: {SDK_ROOT}")
    print("  No administrator access or global configuration changes.")

    if yes:
        return True
    if not sys.stdin.isatty():
        return False
    answer = input("Continue? [y/N] ").strip().lower()
    return answer in ("y", "yes")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def safe_extract(archive: Path, destination: Path) -> None:
    destination_resolved = destination.resolve()
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            output = (destination / member.filename).resolve()
            if not output.is_relative_to(destination_resolved):
                raise SceneCtlError(f"unsafe archive member: {member.filename}")
        package.extractall(destination)


def command_bootstrap(arguments: argparse.Namespace) -> None:
    lock = load_lock()["limine_binary"]
    version = lock["version"]
    target = SDK_ROOT / "limine" / version
    marker = target / ".sceneos-install.json"
    if marker.is_file():
        print(f"Limine {version} is already present at {target}")
        return
    if target.exists():
        raise SceneCtlError(
            f"incomplete bootstrap target exists; inspect and remove it: {target}"
        )

    if not confirm_download(arguments.yes, version, int(lock["size"])):
        raise SceneCtlError("bootstrap cancelled; no files were changed")

    downloads = SDK_ROOT / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / f"limine-binary-{version}.zip"
    partial = archive.with_suffix(".zip.part")

    request = urllib.request.Request(
        lock["url"],
        headers={"User-Agent": "SceneOS-bootstrap/1"},
    )
    print(f"Downloading {lock['url']}")
    with urllib.request.urlopen(request, timeout=60) as response:
        with partial.open("wb") as stream:
            shutil.copyfileobj(response, stream)

    actual_size = partial.stat().st_size
    actual_hash = sha256_file(partial)
    if actual_size != int(lock["size"]) or actual_hash != lock["sha256"]:
        partial.unlink(missing_ok=True)
        raise SceneCtlError(
            "download verification failed; partial archive was removed"
        )
    partial.replace(archive)

    extraction = SDK_ROOT / "extract" / f"limine-{version}"
    if extraction.exists():
        if not extraction.resolve().is_relative_to(SDK_ROOT.resolve()):
            raise SceneCtlError("refusing to clean an extraction path outside .scene-sdk")
        shutil.rmtree(extraction)
    extraction.mkdir(parents=True)
    safe_extract(archive, extraction)

    candidates = list(extraction.rglob("limine-bios.sys"))
    if len(candidates) != 1:
        raise SceneCtlError("unexpected Limine archive layout")
    package_root = candidates[0].parent
    target.parent.mkdir(parents=True, exist_ok=True)
    installing = target.with_name(target.name + ".installing")
    if installing.exists():
        if not installing.resolve().is_relative_to(SDK_ROOT.resolve()):
            raise SceneCtlError("refusing to clean a path outside .scene-sdk")
        shutil.rmtree(installing)
    shutil.copytree(package_root, installing)
    (installing / ".sceneos-install.json").write_text(
        json.dumps(
            {
                "component": "limine-binary",
                "version": version,
                "archive_sha256": lock["sha256"],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    installing.rename(target)
    shutil.rmtree(extraction)
    print(f"Installed Limine {version} at {target}")
    print("Run `python tools/scenectl.py doctor` to check remaining host tools.")


def command_configure(arguments: argparse.Namespace) -> None:
    cmake = find_tool("cmake")
    if cmake is None:
        raise SceneCtlError("CMake was not found; run doctor for details")
    clang = find_tool("clang")
    lld = find_tool("lld")
    nasm = find_tool("nasm")
    ninja = find_tool("ninja")
    if clang is None or lld is None or nasm is None or ninja is None:
        raise SceneCtlError("compiler or build tool is missing; run doctor")

    environment = os.environ.copy()
    environment["SCENEOS_CLANG"] = clang
    environment["SCENEOS_LLD"] = lld
    environment["SCENEOS_NASM"] = nasm
    environment["PATH"] = os.pathsep.join([
        str(Path(ninja).parent),
        str(Path(clang).parent),
        environment.get("PATH", ""),
    ])
    self_test = "ON" if getattr(arguments, "exception", False) else "OFF"
    page_fault_test = (
        "ON" if getattr(arguments, "page_fault", False) else "OFF"
    )
    keyboard_test = (
        "ON" if getattr(arguments, "keyboard", False) else "OFF"
    )
    text_input_test = "ON" if getattr(arguments, "text_input", False) else "OFF"
    mouse_test = "ON" if getattr(arguments, "mouse", False) else "OFF"
    force_tsc_clock = (
        "ON" if getattr(arguments, "tsc_clock", False) else "OFF"
    )
    clock_test = "ON" if getattr(arguments, "clock_check", False) else "OFF"
    local_timer_test = "ON" if getattr(arguments, "lapic_timer", False) else "OFF"
    force_pic = "ON" if getattr(arguments, "pic", False) else "OFF"
    configure_options = []
    if getattr(arguments, "fresh", False):
        # Validate the destination before CMake removes its generated cache.
        checked_build_directory(arguments.preset)
        configure_options.append("--fresh")
    run(
        [
            cmake,
            *configure_options,
            "--preset", arguments.preset,
            f"-DSCENEOS_EXCEPTION_SELF_TEST={self_test}",
            f"-DSCENEOS_PAGE_FAULT_SELF_TEST={page_fault_test}",
            f"-DSCENEOS_KEYBOARD_SELF_TEST={keyboard_test}",
            f"-DSCENEOS_TEXT_INPUT_SELF_TEST={text_input_test}",
            f"-DSCENEOS_MOUSE_SELF_TEST={mouse_test}",
            f"-DSCENEOS_FORCE_TSC_CLOCK={force_tsc_clock}",
            f"-DSCENEOS_CLOCK_SELF_TEST={clock_test}",
            f"-DSCENEOS_LOCAL_TIMER_SELF_TEST={local_timer_test}",
            f"-DSCENEOS_FORCE_PIC={force_pic}",
        ],
        environment=environment,
    )


def command_build(arguments: argparse.Namespace) -> None:
    command_configure(arguments)
    cmake = find_tool("cmake")
    assert cmake is not None
    run([cmake, "--build", "--preset", arguments.preset])


def limine_package_root() -> Path:
    version = load_lock()["limine_binary"]["version"]
    package = SDK_ROOT / "limine" / version
    if not (package / ".sceneos-install.json").is_file():
        raise SceneCtlError(
            "Limine is missing or incomplete; run bootstrap first"
        )
    return package


def find_limine_host_tool(package: Path) -> Path:
    candidates = (
        package / "limine.exe",
        package / "limine-tool-windows-x86" / "limine.exe",
        package / "limine",
    )
    for candidate in candidates:
        if candidate.is_file() and (
            os.name == "nt" or os.access(candidate, os.X_OK)
        ):
            return candidate

    make = shutil.which("make") or shutil.which("gmake")
    compiler = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if make is None or compiler is None:
        raise SceneCtlError(
            "Limine host utility is not built and no make/C compiler was found"
        )
    run([make, f"CC={compiler}"], cwd=package)
    candidate = package / "limine"
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise SceneCtlError("Limine host utility build completed without an executable")
    return candidate


def checked_build_directory(preset: str) -> Path:
    if BUILD_ROOT.resolve() != ROOT / "build":
        raise SceneCtlError("build root must not redirect outside the repository")
    directory = (BUILD_ROOT / preset).resolve()
    if not directory.is_relative_to(BUILD_ROOT.resolve()):
        raise SceneCtlError("invalid build directory")
    return directory


def select_image_tool(requested: str) -> tuple[str, str]:
    candidates = ("xorriso",)
    if requested != "auto":
        candidates = (requested,)
    for name in candidates:
        executable = find_tool(name)
        if executable is not None:
            return name, executable
    raise SceneCtlError(
        "xorriso was not found; set SCENEOS_XORRISO, use QEMU directory "
        "media on Windows, or build the release ISO in CI"
    )


def build_iso_with_xorriso(
    executable: str,
    image_root: Path,
    image: Path,
) -> None:
    run([
        executable,
        "-as", "mkisofs",
        "-R", "-r", "-J",
        "-b", "boot/limine/limine-bios-cd.bin",
        "-no-emul-boot",
        "-boot-load-size", "4",
        "-boot-info-table",
        "-hfsplus",
        "-apm-block-size", "2048",
        "--efi-boot", "boot/limine/limine-uefi-cd.bin",
        "-efi-boot-part",
        "--efi-boot-image",
        "--protective-msdos-label",
        str(image_root),
        "-o", str(image),
    ])


def prepare_boot_tree(arguments: argparse.Namespace) -> tuple[Path, Path]:
    command_build(arguments)

    package = limine_package_root()
    build_directory = checked_build_directory(arguments.preset)
    kernel = build_directory / "bin" / "scene-kernel.elf"
    if not kernel.is_file():
        raise SceneCtlError(f"kernel output is missing: {kernel}")

    image_root = build_directory / "media-root"
    if image_root.exists():
        shutil.rmtree(image_root)
    (image_root / "boot" / "limine").mkdir(parents=True)
    (image_root / "EFI" / "BOOT").mkdir(parents=True)

    shutil.copy2(kernel, image_root / "boot" / kernel.name)
    shutil.copy2(ROOT / "boot" / "limine.conf", image_root / "boot" / "limine")
    shutil.copy2(ROOT / "boot" / "limine.conf", image_root / "EFI" / "BOOT")

    for name in ("limine-bios.sys", "limine-bios-cd.bin", "limine-uefi-cd.bin"):
        shutil.copy2(package / name, image_root / "boot" / "limine")
    shutil.copy2(package / "BOOTX64.EFI", image_root / "EFI" / "BOOT")
    return build_directory, image_root


def command_image(arguments: argparse.Namespace) -> Path:
    image_tool_name, image_tool = select_image_tool(arguments.image_tool)
    package = limine_package_root()
    limine = find_limine_host_tool(package)
    build_directory, image_root = prepare_boot_tree(arguments)

    image = build_directory / "sceneos.iso"
    if image_tool_name != "xorriso":
        raise SceneCtlError(f"unsupported ISO image tool: {image_tool_name}")
    build_iso_with_xorriso(image_tool, image_root, image)
    run([str(limine), "bios-install", str(image)])
    print(f"Created bootable image: {image}")
    return image


def find_qemu_firmware(emulator: str) -> Path:
    qemu_directory = Path(emulator).resolve().parent
    candidates = (
        qemu_directory / "share" / "edk2-x86_64-code.fd",
        Path("/usr/share/qemu/edk2-x86_64-code.fd"),
        Path("/usr/share/OVMF/OVMF_CODE.fd"),
        Path("/usr/share/edk2/x64/OVMF_CODE.fd"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SceneCtlError(
        "QEMU UEFI firmware was not found; use --media iso or install OVMF"
    )


def run_qemu_iso(emulator: str, image: Path) -> None:
    run([
        emulator,
        "-machine", "q35",
        "-m", "256M",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown",
        "-cdrom", str(image),
        "-boot", "d",
    ])


def run_qemu_directory(emulator: str, image_root: Path) -> None:
    firmware = find_qemu_firmware(emulator)
    run([
        emulator,
        "-machine", "q35",
        "-m", "256M",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={firmware.as_posix()}",
        "-serial", "stdio",
        "-no-reboot",
        "-no-shutdown",
        "-drive", f"format=raw,file=fat:rw:{image_root.as_posix()}",
    ])


def command_run(arguments: argparse.Namespace) -> None:
    if arguments.emulator == "qemu":
        emulator = find_tool("qemu")
        if emulator is None:
            raise SceneCtlError("QEMU was not found; run doctor for details")

        use_directory = arguments.media == "directory"
        if arguments.media == "auto":
            image_tools = ("xorriso",)
            use_directory = not any(find_tool(name) for name in image_tools)

        if use_directory:
            _, image_root = prepare_boot_tree(arguments)
            print("Using QEMU UEFI virtual FAT media (no ISO tool required).")
            run_qemu_directory(emulator, image_root)
        else:
            image = command_image(arguments)
            run_qemu_iso(emulator, image)
        return

    if arguments.media == "directory":
        raise SceneCtlError("Bochs requires ISO media; directory media is QEMU-only")
    image = command_image(arguments)

    emulator = find_tool("bochs")
    if emulator is None:
        raise SceneCtlError("Bochs was not found; set SCENEOS_BOCHS or install it")
    build_directory = checked_build_directory(arguments.preset)
    configuration = build_directory / "bochsrc.generated"
    configuration.write_text(
        "\n".join([
            "megs: 256",
            "boot: cdrom",
            f'ata0-master: type=cdrom, path="{image.as_posix()}", status=inserted',
            "log: bochs.log",
            "panic: action=fatal",
            "error: action=report",
            "info: action=report",
            "display_library: win32" if os.name == "nt" else "display_library: x",
            "",
        ]),
        encoding="utf-8",
    )
    run([emulator, "-q", "-f", str(configuration)])


def command_smoke(arguments: argparse.Namespace) -> None:
    emulator = find_tool("qemu")
    if emulator is None:
        raise SceneCtlError("QEMU was not found; run doctor for details")

    build_directory, image_root = prepare_boot_tree(arguments)
    firmware = find_qemu_firmware(emulator)
    serial_log = build_directory / "serial.log"
    serial_log.unlink(missing_ok=True)
    command = [
        emulator,
        "-machine", "q35",
        "-m", "256M",
        "-drive",
        f"if=pflash,format=raw,readonly=on,file={firmware.as_posix()}",
        "-display", "none",
        "-serial", f"file:{serial_log}",
        "-no-reboot",
        "-no-shutdown",
        "-drive", f"format=raw,file=fat:rw:{image_root.as_posix()}",
    ]
    interactive_input_test = (
        arguments.keyboard or arguments.mouse or arguments.text_input
    )
    if interactive_input_test:
        command.extend(["-monitor", "stdio"])
    print("+", " ".join(command))
    if interactive_input_test:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        deadline = time.monotonic() + arguments.timeout
        if arguments.text_input:
            # Match tests/text_input.c. The guest acknowledges every release
            # before the next chord, so modifiers cannot leak between steps.
            keys = (
                "a", "shift-a", "caps_lock", "a", "shift-a", "shift-1",
                "ctrl-a", "alt-a", "caps_lock", "b", "spc", "ret",
                "backspace", "up", "shift_r-z",
            )
            injections = tuple(
                (f"Waiting for text input step: {index}", f"sendkey {key} 30\n")
                for index, key in enumerate(keys)
            )
        elif arguments.keyboard:
            injections = (("Waiting for injected PS/2 key A", "sendkey a\n"),)
        else:
            injections = (("Waiting for injected PS/2 mouse motion", "mouse_move 12 7\n"),)
        try:
            for marker, monitor_command in injections:
                ready = False
                while process.poll() is None and time.monotonic() < deadline:
                    if serial_log.is_file():
                        current = serial_log.read_text(
                            encoding="utf-8", errors="replace"
                        )
                        if "[PANIC]" in current or "[EXCEPTION]" in current:
                            raise SceneCtlError("kernel fault during input injection")
                        if marker in current.splitlines():
                            ready = True
                            break
                    time.sleep(0.05)
                if not ready or process.stdin is None:
                    raise SceneCtlError(f"kernel did not reach input marker: {marker}")
                process.stdin.write(monitor_command)
                process.stdin.flush()
            remaining = max(0.1, deadline - time.monotonic())
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            pass
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()
            if process.stdin is not None:
                process.stdin.close()
    else:
        try:
            subprocess.run(
                command,
                cwd=ROOT,
                check=False,
                timeout=arguments.timeout,
            )
        except subprocess.TimeoutExpired:
            pass

    if not serial_log.is_file():
        raise SceneCtlError("QEMU did not create a serial log")
    output = serial_log.read_text(encoding="utf-8", errors="replace")
    clock_source = (
        "calibrated TSC" if arguments.tsc_clock else "HPET"
    )
    interrupt_controller = "8259A PIC" if arguments.pic else "Local APIC + IOAPIC"
    if arguments.exception:
        expected = (
            "SceneOS kernel bootstrap",
            "Exception IDT: ready",
            "Triggering exception self-test",
            "[EXCEPTION] processor exception",
            "Vector: 3",
            "IST index: 4",
        )
    elif arguments.page_fault:
        expected = (
            "SceneOS kernel bootstrap",
            f"Interrupt controller: {interrupt_controller}",
            "IRQ registration lifecycle: verified",
            "Registered IRQ handlers: 1",
            "Hardware timer ticks observed:",
            f"Clock source: {clock_source}",
            "Clock source frequency Hz:",
            "Timer queue ordering/cancellation: verified",
            "Kernel heap virtual address:",
            "Triggering page-fault self-test",
            "[EXCEPTION] processor exception",
            "Vector: 14",
            "Fault address: 0xffffd00000000000",
            "Page fault cause: non-present page",
            "Page fault access: write",
            "Page fault privilege: supervisor",
        )
    elif arguments.keyboard:
        expected = (
            "SceneOS kernel bootstrap",
            f"Interrupt controller: {interrupt_controller}",
            "IRQ registration lifecycle: verified",
            "PS/2 keyboard: ready",
            "PS/2 mouse: ready",
            "Registered IRQ handlers after input: 3",
            "Hardware timer ticks observed:",
            f"Clock source: {clock_source}",
            "Waiting for injected PS/2 key A",
            "Injected input sequence:",
            "Injected key code: 30",
            "PS/2 scancodes received:",
            "PS/2 key A press: verified",
            "Bootstrap complete",
        )
    elif arguments.mouse:
        expected = (
            "SceneOS kernel bootstrap",
            f"Interrupt controller: {interrupt_controller}",
            "IRQ registration lifecycle: verified",
            "PS/2 keyboard: ready",
            "PS/2 mouse: ready",
            "Registered IRQ handlers after input: 3",
            "Hardware timer ticks observed:",
            f"Clock source: {clock_source}",
            "Waiting for injected PS/2 mouse motion",
            "Injected pointer sequence:",
            "Injected pointer delta X:",
            "Injected pointer delta Y:",
            "PS/2 mouse packets received:",
            "PS/2 mouse motion: verified",
            "Bootstrap complete",
        )
    else:
        expected = (
            "SceneOS kernel bootstrap",
            "Kernel GDT and TSS: ready",
            "Exception IDT: ready",
            "First allocated physical page:",
            "Reclaimed physical page:",
            "Boot page-table root:",
            "SceneOS page-table root:",
            "SceneOS-owned page tables:",
            f"Interrupt controller: {interrupt_controller}",
            "IRQ registration lifecycle: verified",
            "Registered IRQ handlers: 1",
            "PS/2 keyboard: ready",
            "PS/2 mouse: ready",
            "Registered IRQ handlers after input: 3",
            "PIT timer frequency: 100",
            "Hardware timer ticks observed:",
            f"Clock source: {clock_source}",
            "Clock source frequency Hz:",
            "Monotonic clock advance ns:",
            "Timer queue ordering/cancellation: verified",
            "Timer queue pending events: 0",
            "Released mapped physical page:",
            "Kernel heap virtual address:",
            "Kernel heap split/coalesce/reuse: verified",
            "Kernel heap committed pages:",
            "Kernel heap allocated bytes: 0",
            "Bootstrap complete",
        )
    if not arguments.exception:
        if not arguments.pic:
            expected += ("PIT interrupt GSI:",)
        expected += (
            "TSC calibration reference: PIT",
            "TSC measured frequency Hz:",
            "TSC CPUID frequency Hz:",
            "TSC invariant:",
            "TSC frequency check:",
            "TSC frequency tolerance ppm: 50000",
        )
    if arguments.clock_check:
        expected += ("Clock diagnostic edge cases: verified",)
    if arguments.text_input:
        expected += (
            "Text layout edge cases: verified",
            "UTF-8 scalar boundaries: verified",
            "PS/2 text sequence: aAAa!b Z",
            "PS/2 text event count: 8",
            "Input events dropped: 0",
            "Text key presses/releases and shortcuts: verified",
            "PS/2 text input: verified",
        )
    if arguments.lapic_timer:
        expected += ("Local timer arithmetic boundaries: verified",)
        if not arguments.tsc_clock:
            expected += ("PIT cadence against HPET: verified",)
        if arguments.pic:
            expected += ("Local timer unavailable: PIT fallback verified",)
        else:
            reference = "PIT" if arguments.tsc_clock else "HPET"
            expected += (
                f"Local timer calibration reference: {reference}",
                "Local timer counter frequency Hz:",
                "Local timer periodic interrupts:",
                "Local timer periodic delivery/stop: verified",
                "Local timer one-shot delivery: verified",
                "Local timer pending interrupt drain/rearm: verified",
                "PIT compatibility ticks: verified",
            )
    if "[PANIC]" in output:
        raise SceneCtlError("kernel panic during serial smoke test")
    missing = [text for text in expected if text not in output]
    if missing:
        raise SceneCtlError(
            "serial smoke test missed: " + ", ".join(missing)
        )
    print(f"Serial smoke test passed: {serial_log}")


def command_clean(arguments: argparse.Namespace) -> None:
    cmake = find_tool("cmake")
    directory = checked_build_directory(arguments.preset)
    if cmake is None:
        raise SceneCtlError("CMake was not found")
    if not directory.is_dir():
        print(f"Nothing to clean for preset {arguments.preset}")
        return
    run([cmake, "--build", str(directory), "--target", "clean"])


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="scenectl")
    subcommands = result.add_subparsers(dest="command", required=True)

    doctor = subcommands.add_parser("doctor", help="diagnose host tools")
    doctor.set_defaults(handler=command_doctor)

    bootstrap = subcommands.add_parser(
        "bootstrap",
        help="download pinned project-local boot dependencies",
    )
    bootstrap.add_argument(
        "--yes",
        action="store_true",
        help="accept the displayed project-local download plan",
    )
    bootstrap.set_defaults(handler=command_bootstrap)

    for name, handler in (
        ("configure", command_configure),
        ("build", command_build),
        ("image", command_image),
        ("clean", command_clean),
    ):
        command = subcommands.add_parser(name)
        command.add_argument("--preset", choices=("dev", "release"), default="dev")
        if name in ("configure", "build"):
            command.add_argument(
                "--fresh",
                action="store_true",
                help="regenerate CMake cache after moving the repository",
            )
        if name == "image":
            command.add_argument(
                "--image-tool",
                choices=("auto", "xorriso"),
                default="auto",
            )
        command.set_defaults(handler=handler)

    run_command = subcommands.add_parser("run")
    run_command.add_argument("--preset", choices=("dev", "release"), default="dev")
    run_command.add_argument("--emulator", choices=("qemu", "bochs"), default="qemu")
    run_command.add_argument(
        "--image-tool",
        choices=("auto", "xorriso"),
        default="auto",
    )
    run_command.add_argument(
        "--media",
        choices=("auto", "iso", "directory"),
        default="auto",
        help="auto uses QEMU UEFI directory media if no ISO tool is available",
    )
    run_command.set_defaults(handler=command_run)

    smoke = subcommands.add_parser(
        "smoke",
        help="run a bounded headless QEMU UEFI serial test",
    )
    smoke.add_argument("--preset", choices=("dev", "release"), default="dev")
    smoke.add_argument("--timeout", type=int, default=10)
    fault_tests = smoke.add_mutually_exclusive_group()
    fault_tests.add_argument(
        "--text-input",
        action="store_true",
        help="validate US layout, Unicode/UTF-8, and injected PS/2 text",
    )
    fault_tests.add_argument(
        "--lapic-timer",
        action="store_true",
        help="validate local timer calibration, delivery, stop, and rearm",
    )
    fault_tests.add_argument(
        "--clock-check",
        action="store_true",
        help="validate CPUID decoding and clock diagnostic boundary cases",
    )
    fault_tests.add_argument(
        "--exception",
        action="store_true",
        help="trigger int3 and validate the exception dispatch path",
    )
    fault_tests.add_argument(
        "--page-fault",
        action="store_true",
        help="write an unmapped address and validate page-fault diagnostics",
    )
    fault_tests.add_argument(
        "--keyboard",
        action="store_true",
        help="inject a key through QEMU and validate PS/2 input delivery",
    )
    fault_tests.add_argument(
        "--mouse",
        action="store_true",
        help="inject motion through QEMU and validate PS/2 pointer delivery",
    )
    smoke.add_argument(
        "--tsc-clock",
        action="store_true",
        help="disable HPET and validate the calibrated-TSC fallback",
    )
    smoke.add_argument(
        "--pic",
        action="store_true",
        help="keep the legacy PIC controller and validate compatibility",
    )
    smoke.set_defaults(handler=command_smoke)

    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        result = arguments.handler(arguments)
        if isinstance(result, Path):
            return 0
        return 0
    except (SceneCtlError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
