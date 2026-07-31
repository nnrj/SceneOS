# Coding and interface guidelines

- Kernel C uses C17 with strict warnings treated as errors by default.
- Freestanding code must not depend implicitly on host libc or host headers.
- Hardware fields use fixed-width integers; addresses use full-width types.
- Architecture assembly belongs only under `arch/<architecture>`.
- Public interfaces use the `scene_` prefix and guarded headers.
- Modules communicate through interfaces and opaque objects, not another
  module's mutable globals.
- Failure is an explicit error or panic; state corruption is never ignored.
- New public contracts update both `docs/zh/api` and `docs/en/api`.
- Third-party files are not edited in place; upgrades record source, license,
  commit, and checksum.
- Script download, cleanup, and overwrite scopes must be statically auditable.

