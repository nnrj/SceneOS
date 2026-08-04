# Hardware interrupts and early time base

## Boot sequence

After installing the exception IDT, the architecture layer remaps the 8259A to
vectors 32–47 and masks every IRQ. Once the memory subsystem owns CR3, the
interrupt layer validates the Limine-provided ACPI RSDP, walks XSDT/RSDT,
checks the MADT checksum, and reads Local APIC, IOAPIC, and Interrupt Source
Override records. MMIO pages are mapped into dedicated SceneOS higher-half
addresses, keeping physical registers outside core code.

When Local APIC mode is available, initialization masks its local vector
entries and every IOAPIC redirection entry, installs a spurious vector, then
opens only the GSI carrying the PIT. A typical Q35 MADT overrides ISA IRQ0 to
GSI 2; SceneOS consumes the table instead of hard-coding that override. If
RSDP or MADT discovery fails, the 8259A compatibility path remains active.

## Interrupt entry

NASM emits entries for legacy IRQ0–15 at vectors 32–47 and vector 255 for APIC
spurious interrupts. The common entry saves general registers, aligns the C
call stack for the SysV ABI, invokes the dispatcher, restores state, and
returns with `iretq`. IRQ0 increments the 64-bit tick count before sending a
Local APIC or 8259A EOI according to the active controller.

The core owns a fixed 16-entry legacy IRQ registry. Drivers attach a handler
through `scene_irq_attach()`, and registration becomes visible only after the
architecture route succeeds. Duplicate attachment, an out-of-range line, and
a mismatched detach fail. `scene_irq_detach()` masks the IOAPIC/PIC input before
clearing the handler.

Every ISA Interrupt Source Override in MADT is retained as per-line GSI and
flags instead of handling IRQ0 alone. PIT now attaches IRQ0 through the same
interface. The common interrupt dispatcher only invokes the registered entry,
then sends EOI through the active controller. Shared IRQs are not supported,
and attach/detach are currently restricted to serial boot; concurrency and
dynamic vector allocation remain future work.

## PIT and watchdog

PIT channel 0 runs in rate-generator mode at approximately 100 Hz. The boot
self-test enables interrupts and waits for three ticks. The wait loop also
uses a two-billion-TSC-cycle startup watchdog so broken routing cannot leave
QEMU waiting forever.

ACPI traversal is now shared rather than private to the APIC driver. It checks
RSDP, XSDT/RSDT, every target-table checksum, and firmware-provided length
bounds. Q35 exposes a 64-bit 100 MHz HPET; SceneOS maps its MMIO page, resets
and enables the main counter, and selects it as the preferred monotonic source.

Without a suitable HPET, the core clock waits for a PIT tick boundary and
calibrates TSC frequency over eight ticks. `python tools/scenectl.py smoke
--tsc-clock` forces this fallback. QEMU currently does not advertise invariant
TSC, so the fallback does not promise accuracy across CPUs, sleep, migration,
or frequency-state changes.

Both sources use the same quotient-and-remainder nanosecond conversion instead
of directly multiplying `elapsed * 1,000,000,000`. The clock origin is source
initialization, not boot or the Unix epoch. CPUID frequency cross-checking and
Local APIC timer evaluation remain future work.

## Early timer queue

The core layer uses a fixed 32-entry min-heap ordered by nanosecond deadline
and increasing ID. It supports absolute/relative scheduling and cancellation.
`scene_timer_run_due()` removes due entries and invokes callbacks in ordinary
kernel context; IRQ0 only updates the tick counter and never runs arbitrary
callbacks. The queue is currently BSP-serial, without locking or dynamic
growth.

The boot self-test schedules 5 ms and 10 ms callbacks, cancels a 7.5 ms item,
then verifies order, cancellation, and an empty queue. PIT IRQs provide a
bounded wait window, but expiration uses only the selected monotonic clock and
does not assume that a number of IRQs equals exact wall-clock time.

## Automated validation

```powershell
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
```

The default test requires `Interrupt controller: Local APIC + IOAPIC`, the PIT
GSI, IRQ lifecycle verification, one registered handler, the 100 Hz frequency,
at least three hardware ticks, `Clock source: HPET`, and timer
ordering/cancellation verification. `smoke --tsc-clock` requires
`Clock source: calibrated TSC`. The page-fault test requires the default steps
too, proving that exception and IRQ/time paths coexist.
