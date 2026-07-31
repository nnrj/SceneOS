# Vision and design principles

SceneOS is an experimental modern operating system for desktop computing,
automation, and long-term compatibility. It is a clean implementation informed
by the behavior, features, and failure history of NNOS, not an in-place 64-bit
upgrade of the NNOS source tree.

## Product direction

> Unix-friendly core, Windows-friendly desktop, AI-friendly automation.

- Native command-line behavior follows Unix conventions: `/` path separators,
  case sensitivity, pipelines, redirection, and composable commands.
- The desktop learns from Windows window management, taskbar, start surface,
  file management, shortcuts, installation experience, and compatibility
  culture.
- GUI and CLI are both first-class interfaces. The GUI is decoupled from the
  kernel but is not an optional afterthought.
- AI is a default system service and structured automation surface, never
  kernel code.
- The first target is x86-64, with architecture boundaries that permit a later
  ARM64 port.

## Kernel principles

- Begin with a modular monolithic kernel and permit a later hybrid evolution.
- Modules communicate through explicit interfaces and opaque handles.
- GUI, desktop, AI, and high-level network protocols begin in user space.
- Serial logging, panic handling, and reproducible VM tests are infrastructure.
- ELF64 is native. NNOS `.nex` is only a possible future compatibility target.

## Engineering principles

- Windows and Linux share one CMake project and the same build semantics.
- IDE integration is optional convenience, never a build prerequisite.
- One-command execution is a supported interface.
- Bootstrap tools do not silently modify the registry, global `PATH`, system
  directories, or shell profiles.
- Third-party tools and dependencies are version-pinned and checksum-verified.

## Compatibility stages

Before 1.0, API and ABI corrections are allowed and repository callers change
with them. After the first stable release, compatibility becomes a release
gate: public ABIs primarily grow instead of changing, old binaries stay in
continuous tests, and side-by-side runtimes, manifests, and user-space shims
preserve behavior where necessary.

