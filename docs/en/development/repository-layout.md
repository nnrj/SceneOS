# Repository layout

```text
boot/                 Limine configuration and future boot resources
cmake/toolchains/     host-independent cross-toolchain descriptions
docs/zh, docs/en      peer bilingual documentation
kernel/include/scene  kernel module interfaces
kernel/src/arch       architecture-specific implementation
kernel/src/boot       boot protocol adapters
kernel/src/core       kernel foundations
kernel/src/graphics   current framebuffer validation layer
kernel/linker         architecture linker scripts
third_party/          pinned third-party sources and provenance
tools/                build, image, diagnosis, and bootstrap orchestration
```

`build/` and `.scene-sdk/` are untracked. NNOS remains a read-only reference in
its neighboring repository; its build artifacts and complete source tree do
not belong in SceneOS.

