# Third-party components

SceneOS source code is distributed under the MIT License unless a file or
directory states otherwise.

| Component | Purpose | License | Distribution |
| --- | --- | --- | --- |
| Limine protocol header | Boot protocol declarations | 0BSD | Vendored in `third_party/limine/` with provenance and checksum |
| Limine bootloader | BIOS/UEFI bootloader and host utility | BSD-2-Clause | Downloaded on demand to ignored `.scene-sdk/` |

Build tools such as CMake, Ninja, LLVM, NASM, QEMU, and xorriso are
not part of SceneOS and are not redistributed by this repository.
