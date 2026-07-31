# Compatibility policy

## Before the stable release

- Cross-commit API and ABI compatibility is not guaranteed.
- Correctness, clean boundaries, and maintainability take priority.
- API changes update repository callers and both documentation languages in the
  same change.
- Unreleased interfaces do not accumulate permanent compatibility burden.

## From 1.0 onward

- The system-call ABI primarily grows instead of changing.
- Public structures do not expose kernel-internal layouts.
- Public libraries have independent ABI major versions; incompatible majors
  can coexist.
- Deprecation includes diagnostics, migration guidance, and a reasonable shim
  period.
- Configuration, data formats, command behavior, errors, and window protocols
  are compatibility surfaces too.
- CI retains historical application binaries and representative data.
- Severe security issues may break compatibility, with documented impact and
  migration.

## Windows and Linux compatibility personalities

Native SceneOS semantics remain Unix-like. Drive letters, backslashes,
case-insensitive lookup, and foreign system calls belong to independent
user-space compatibility personalities. Before 1.0, SceneOS does not promise
Windows or Linux binary compatibility; its own long-term compatibility comes
first.

