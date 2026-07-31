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
QEMU permanently sleeping in `hlt`. The tick count is an observable early
time base, not yet a nanosecond monotonic clock.

The next step is to calibrate the TSC, evaluate invariant TSC, HPET, and the
Local APIC timer, define a common monotonic unit, and build an early timer
queue that does not depend on dynamic allocation.

## Automated validation

```powershell
python tools/scenectl.py smoke
python tools/scenectl.py smoke --page-fault
```

The default test requires `Interrupt controller: Local APIC + IOAPIC`, the PIT
GSI, the 100 Hz frequency, and at least three hardware ticks. The page-fault
test also requires completed interrupt initialization, proving that exception
and IRQ paths coexist.
