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
Before enabling device IRQs, fallback globally disables any firmware-enabled
Local APIC through IA32_APIC_BASE so a masked LINT0 cannot block direct 8259A
delivery. `smoke --pic` forces this path. This is a BSP boot choice, not runtime
controller switching; see [Intel SDM Vol. 3A §12.4.3](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf).

## Interrupt entry

NASM emits entries for legacy IRQ0–15 at vectors 32–47, local timer vector 48,
and APIC spurious vector 255. The common entry saves general registers, aligns the C
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

PIT channel 0 uses mode 2 rate generation (control byte `0x34`) at approximately
100 Hz. The boot
self-test enables interrupts and waits for three ticks. The wait loop also
uses a two-billion-TSC-cycle startup watchdog so broken routing cannot leave
QEMU waiting forever.

ACPI traversal is now shared rather than private to the APIC driver. It checks
RSDP, XSDT/RSDT, every target-table checksum, and firmware-provided length
bounds. Q35 exposes a 64-bit 100 MHz HPET; SceneOS maps its MMIO page, resets
and enables the main counter, and selects it as the preferred monotonic source.

The core clock waits for a PIT tick boundary and measures TSC frequency over
eight ticks. The HPET path uses this measurement for diagnostics; without a
suitable HPET, it supplies the TSC fallback frequency. A failed measurement
does not disable a working HPET. Initialization fails only when neither HPET
nor a measured fallback is available. `python tools/scenectl.py smoke
--tsc-clock` forces the fallback. The current default QEMU CPU does not
advertise invariant TSC, so the fallback does not promise accuracy across
CPUs, sleep, migration, or frequency-state changes.

The architecture layer uses `ECX * EBX / EAX` as defined by
[Intel SDM CPUID.15H](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).
It requires a supported leaf, nonzero denominator/numerator, and an enumerated
crystal frequency. A 64-bit product avoids truncating the ratio first.
Missing data returns zero; no model-specific crystal guess or CPUID.16H
processor-base-frequency substitution is made.

Diagnostics retain the reported frequency, PIT measurement, and invariant
flag. Comparisons accept frequencies from 1 MHz through 10 GHz and use a
50,000 ppm (5%) tolerance relative to the reported value, checking the limit
before rounding the displayed ppm. Status is `unavailable`, `invalid frequency`,
`unmeasured`, `within tolerance`, or `mismatch`. Missing CPUID frequency data
does not block boot. Invalid reports or mismatches only warn; they never
override HPET priority or the measured fallback frequency. One short sample
and the invariant flag do not prove cross-CPU synchronization, migration
safety, or long-term hardware accuracy.

Both sources use the same quotient-and-remainder nanosecond conversion instead
of directly multiplying `elapsed * 1,000,000,000`. The clock origin is source
initialization, not boot or the Unix epoch. Longer-window clock accuracy
checks remain future work.

## Optional BSP Local APIC timer

`scene/local_timer.h` provides a hardware-interrupt event counter separate from
the monotonic clock and software timer queue. Normal boot neither calibrates
nor starts it. `smoke --lapic-timer` compiles a separate self-test and stops the
hardware afterward. Dedicated vector 48 uses the common save/return path; the
dispatcher updates local-timer state and sends Local APIC EOI. It consumes no
legacy IRQ0–15 slot and invokes no arbitrary callbacks inside the IRQ.

The implementation uses the xAPIC 32-bit down-counter with a fixed divide by
16. Calibration waits for a PIT boundary, then counts with the timer interrupt
masked over 2–16 PIT ticks (eight in the self-test). When available, raw HPET
counts measure elapsed time; otherwise PIT ticks and frequency supply the
reference. The reported frequency is the divided counter rate, not the bus or
CPU frequency. Waits retain a bounded watchdog. Counter expiration, no
decrement, an invalid reference interval, and arithmetic overflow all fail.

One-shot delays round upward to a count; periodic rates use the nearest nonzero
reload value. Start writes initial count last; stop masks the LVT and writes
initial count zero, following [Intel SDM Vol. 3A §12.5.4](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf).
Stop cannot retract an interrupt already in IRR/ISR. Stopped state ignores
such an event, but the dispatcher still sends EOI. Rearm checks IRR, ISR, and
LVT delivery status and fails while an old event remains; callers may retry
after allowing interrupts to drain. Starting an active timer also fails
without replacing it. Configuration and snapshots save/restore the caller's
IF state instead of unconditionally enabling interrupts.

This remains a BSP-serial evaluation interface for ordinary kernel context.
A short calibration does not establish long-term rate accuracy. Per-CPU state,
TSC-deadline, ARAT/power-state validation, and scheduling/sleep-wakeup integration
remain pending. These tests do not prove deep-sleep or SMP reliability on real
hardware. The default PIT and software timer queue retain their existing path.

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
python tools/scenectl.py smoke --clock-check --timeout 10
python tools/scenectl.py smoke --clock-check --tsc-clock --timeout 10
python tools/scenectl.py smoke --lapic-timer --timeout 10
python tools/scenectl.py smoke --lapic-timer --tsc-clock --timeout 10
python tools/scenectl.py smoke --lapic-timer --pic --timeout 10
```

The default test requires `Interrupt controller: Local APIC + IOAPIC`, the PIT
GSI, IRQ lifecycle verification, one registered handler, the 100 Hz frequency,
at least three hardware ticks, `Clock source: HPET`, and timer
ordering/cancellation verification. `smoke --tsc-clock` requires
`Clock source: calibrated TSC`. The page-fault test requires the default steps
too, proving that exception and IRQ/time paths coexist.

All modes except the early exception test also require TSC diagnostic fields.
`--clock-check` adds a separate test translation unit covering missing leaves,
zero fields, fractional ratios, wide products, both tolerance boundaries,
missing measurements, and unsupported frequency ranges. Success emits
`Clock diagnostic edge cases: verified`. The current default QEMU CPU exposes
no usable CPUID.15H frequency, so its actual status is `unavailable`. Synthetic
cases validate decoding and classification, not a physical CPU's frequency.

Local-timer checks cover arithmetic boundaries, uninitialized/invalid/repeated
requests, periodic delivery followed by stop, exactly one one-shot delivery,
and rearm after draining an old interrupt. The drain test deliberately clears
IF until an interrupt is pending, verifies immediate rearm after stop fails,
then restores IF and checks that the old event is ignored and rearm succeeds.
It also checks continuing PIT ticks and an unchanged IRQ registration count.
`--tsc-clock` disables the HPET path, selecting PIT calibration. `--pic` requires
an unavailable local timer with a working PIT. `--pic --keyboard` and
`--pic --mouse` cover master-PIC and cascaded slave-PIC input routes. Any
`[PANIC]` makes smoke fail.

When HPET is available, the test also measures the rate of eight PIT ticks
against it and requires 75%–125% of the configured rate. This broad tolerance
catches errors such as doubled delivery; it is not an accuracy certification.
The old control byte `0x36` actually selected mode 3. This host's QEMU IOAPIC
path showed roughly doubled ticks, making PIT calibration about half the HPET
result. The intended mode 2 and this regression check now address that case.
