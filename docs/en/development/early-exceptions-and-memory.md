# Early exceptions and physical memory

## Descriptor tables

SceneOS installs its own GDT after boot: null, 64-bit kernel code, kernel data,
and a 16-byte TSS descriptor. Code and data descriptor accessed bits are
preset. The TSS descriptor resides in a writable GDT because `LTR` changes an
available TSS descriptor to the busy state.

The TSS provides a 32 KiB Ring 0 stack and four 16 KiB IST stacks: double fault
uses IST1, NMI uses IST2, machine check uses IST3, and exception self-tests may
temporarily use IST4. The I/O bitmap offset is placed after the TSS, so user
mode currently has no direct port-I/O permission.

The IDT currently covers vectors 0–31. NASM entries normalise stack layouts
with and without hardware error codes, save general-purpose registers, and
pass a `scene_exception_frame` to the C dispatcher. The current policy reports
the vector, error code, RIP, RFLAGS, and IST index, then halts. A resumable
interrupt return path and per-thread `RSP0` updates are required before user
mode.

## Early physical page allocator

The boot adapter copies only Limine usable regions into kernel-owned data.
Each region stores its own allocation bitmap at its beginning. Metadata
therefore scales sparsely with usable memory instead of creating a giant
global bitmap for holes below the highest physical address. The allocator:

- uses 4 KiB pages;
- aligns region boundaries inward;
- reserves the first 1 MiB of physical address space;
- skips overflowing or empty regions;
- distinguishes allocation success from exhaustion with a Boolean result;
- supports page release and rejects unaligned, metadata, and double frees;
- updates search hints so released pages can be reused.

Arbitrary reservation tracking, contiguous multi-page allocation, concurrency
protection, and richer statistics remain future work.

## Paging

The paging module reads the active CR3 and accesses Limine-created four-level
tables through the HHDM. New mappings allocate and clear intermediate tables
on demand, support supervisor/user, writable, and executable flags, and mark
data pages NX by default. The interface rejects non-canonical or unaligned
addresses, huge intermediate entries, and replacement of existing leaves; it
executes `invlpg` after a successful mapping.

During initialization SceneOS recursively clones all present four-level table
structures, preserves leaf physical mappings and huge pages, then loads a new
CR3. Clone records preserve aliases and avoid cycles from recursive mappings.
Subsequent mappings modify only SceneOS-owned tables.

The paging layer can remove a 4 KiB leaf, execute `invlpg`, and return the old
physical page for release. Reclaiming empty intermediate tables, splitting
huge pages, and full address-space destruction remain future work.

## Early kernel heap

The heap begins at `0xffffc00000000000`, reserves at most 16 MiB, grows by four
pages, and asks the paging layer for physical pages. Blocks carry doubly linked
metadata. Allocation supports power-of-two alignment and tail splitting;
release validates prefix and block magic, rejects double frees, and coalesces
adjacent free blocks in both directions.

Tests cover cross-page access, splitting, coalescing, reuse without growth,
full release back to zero allocated bytes, and double-free rejection. The heap
does not yet provide locking, slabs/size classes, poisoning, or tail-page
unmapping back to the physical allocator.

Page-fault diagnostics report the error code, RIP, RFLAGS, CR2, access type,
and user/supervisor privilege.

## Validation

```text
python tools/scenectl.py smoke
python tools/scenectl.py smoke --exception
python tools/scenectl.py smoke --page-fault
```

These commands use QEMU UEFI virtual FAT media without an ISO tool. The normal
test validates paging, the multi-page heap, and boot completion. The exception
test triggers `int3`; the page-fault test writes an unmapped address to verify
the hardware-error-code entry and CR2 diagnostics.
