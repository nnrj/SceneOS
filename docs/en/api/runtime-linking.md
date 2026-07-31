# Runtime linking and dependency policy

SceneOS supports both self-contained applications and shared runtimes. Dynamic
dependencies are an optimization, not a mandatory distribution model.

Default search order:

1. fully bound path or identity in the application manifest;
2. private application `lib/`;
3. application-private side-by-side version store;
4. user shared libraries;
5. system shared libraries.

The current working directory and arbitrary `PATH` entries are excluded from
the default library search path. Runtime loading never downloads dependencies
or solves broad version ranges opportunistically.

Public library identities include an ABI major version, such as `scene-ui@1`.
Incompatible majors can coexist, and installation transactions must support
rollback. Applications may link statically or carry private libraries; system
updates must not overwrite private dependencies. The concrete ELF dynamic
loader ABI will be specified before its implementation.

