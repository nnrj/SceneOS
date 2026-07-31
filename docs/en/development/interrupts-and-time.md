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

Only IRQ0 is currently open. All other IOAPIC and PIC inputs remain masked, so
drivers must not claim vectors directly. A future general IRQ layer must own
vector allocation, sharing, masking, deregistration, and concurrency.

## PIT and watchdog

PIT channel 0 runs in rate-generator mode at approximately 100 Hz. The boot
self-test enables interrupts and waits for three ticks. The wait loop also
uses a two-billion-TSC-cycle startup watchdog so broken routing cannot leave
QEMU waiting forever.

The core clock waits for a PIT tick boundary and calibrates TSC frequency over
eight ticks. Nanosecond conversion divides into quotient and remainder instead
of directly multiplying `elapsed * 1,000,000,000`, avoiding overflow. The
clock origin is calibration completion, not boot or the Unix epoch.

QEMU currently does not advertise invariant TSC, so this is only an early
monotonic source. It does not yet promise accuracy across CPUs, sleep,
migration, or frequency-state changes. Future work must use CPUID, HPET, and
the Local APIC timer to select and cross-check clock sources.

## Early timer queue

The core layer uses a fixed 32-entry min-heap ordered by nanosecond deadline
and increasing ID. It supports absolute/relative scheduling and cancellation.
`scene_timer_run_due()` removes due entries and invokes callbacks in ordinary
kernel context; IRQ0 only updates the tick counter and never runs arbitrary
callbacks. The queue is currently BSP-serial, without locking or dynamic
growth.

## Automated validation

```powershell
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
```

The default test requires `Interrupt controller: Local APIC + IOAPIC`, the PIT
GSI, the 100 Hz frequency, at least three hardware ticks, TSC calibration, and
timer ordering/cancellation verification. The page-fault test requires those
steps too, proving that exception and IRQ/time paths coexist.
