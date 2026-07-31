# Boot contract

SceneOS currently boots with Limine protocol base revision 6. The kernel
requests:

- bootloader name and version;
- HHDM offset;
- ACPI RSDP;
- memory map;
- first framebuffer.

Only a supported Limine base revision is mandatory. The framebuffer and RSDP
are optional. A missing framebuffer produces a log and a headless boot; a
missing or invalid RSDP prevents APIC activation and retains the 8259A
compatibility path. The physical page allocator claims `USABLE` memory-map
regions on a per-region basis.

The entry point is `kmain`, linked at `0xffffffff80000000`. Limine requests
occupy a distinct read-only PT_LOAD segment, while code, read-only data, and
writable data receive separate permission segments.

The boot adapter converts third-party protocol structures into SceneOS-owned
structures. No kernel code outside the adapter may depend directly on
`limine.h`, allowing protocol upgrades or additional boot paths later.
