# Interrupt and time interfaces

These are kernel module interfaces, not a stable driver ABI.

## Interrupt control

- `scene_arch_interrupts_init()` installs IRQ IDT entries, remaps the 8259A,
  and masks it.
- `scene_arch_interrupts_activate_apic(boot_info)` discovers and enables the
  Local APIC and IOAPIC from ACPI MADT.
- `scene_arch_interrupts_enable()` / `disable()` control IF on the current CPU.
- `scene_arch_interrupts_save()` saves interrupt state and disables local
  maskable interrupts, returning an opaque token for `restore(state)`.
- `scene_arch_interrupts_restore(state)` restores only interrupt enable state.
  Pair tokens on the same CPU in stack order; this is neither full register
  restoration nor an SMP lock.
- `scene_arch_interrupts_enabled()` queries the current CPU's interrupt state.
- `scene_arch_interrupt_controller()` returns a diagnostic controller name.

The required order is exception IDT, PIC-compatible initialization, paging
ownership, APIC activation, device timer initialization, and only then `sti`.
The current implementation covers the BSP and one IOAPIC as an early boot
configuration.

## IRQ registration

- `scene_irq_attach(irq, handler, context)` installs an exclusive handler and
  enables its architecture input; failed routing rolls registration back.
- `scene_irq_detach(irq, handler, context)` requires an exact handler/context
  match and masks the input before clearing registration.
- `scene_irq_dispatch(irq)` is called by the architecture entry and returns
  `false` when no handler exists.
- `scene_irq_registered_count()` returns the current exclusive registration
  count.

`SCENE_IRQ_LINE_COUNT` is currently 16 and describes legacy ISA IRQ lines, not
arbitrary IDT vectors or GSIs. A line accepts one handler. Shared interrupts,
priority, concurrent registration, and user-visible capabilities are not yet
available. Drivers must not edit the IDT or IOAPIC redirection entries or send
EOI themselves.

## Time interface

- `scene_timer_init(frequency_hz)` configures PIT channel 0 and routes IRQ0.
- `scene_timer_frequency()` returns the divisor-derived frequency, which may
  differ from the requested value.
- `scene_timer_ticks()` returns ticks observed since initialization.
- `scene_timer_wait_ticks(count)` waits for a relative number of ticks and
  returns `false` if its startup watchdog expires.

`scene_timer_ticks()` currently relies on aligned x86-64 64-bit access. It has
no SMP synchronization guarantee and is not a stable wall clock, a
timeout-safe source, or a user-space ABI. The PIT tick handler is an internal
timer-module implementation installed through the IRQ registry; drivers must
not call it directly.

## Monotonic clock

- `scene_clock_init(boot_info, calibration_ticks)` prefers ACPI HPET and
  measures TSC against PIT ticks for diagnostics; without HPET, that measured
  frequency supplies the fallback.
- `scene_clock_ready()` reports whether the selected source is ready.
- `scene_clock_source_name()` returns `HPET` or `calibrated TSC`.
- `scene_clock_source_stable()` returns `true` for HPET and follows the CPU's
  invariant-TSC declaration for the fallback.
- `scene_clock_source_frequency_hz()` returns the active source frequency.
- `scene_clock_monotonic_ns()` returns nanoseconds from the calibration origin.
- `scene_clock_get_diagnostics(out)` copies the initialization diagnostic
  snapshot. An unready clock or null pointer returns `false` without writing.
- `scene_clock_tsc_check_name()` returns the diagnostic string for the current
  TSC frequency comparison status.

`scene_clock_diagnostics` contains `tsc_reported_hz`, `tsc_measured_hz`,
`tsc_deviation_ppm`, `tsc_invariant`, and `tsc_check`. The reported frequency
comes from `scene_arch_timestamp_counter_frequency_hz()`; missing CPUID.15H
or required fields produce zero. A zero measured frequency means PIT
measurement failed. The caller owns the copied snapshot, not internal state.

`tsc_check` distinguishes `UNAVAILABLE` (no report), `INVALID` (a nonzero
frequency outside 1 MHz–10 GHz), `UNMEASURED` (a valid report but no measurement),
`MATCH`, and `MISMATCH`, all prefixed with `SCENE_CLOCK_TSC_`. Deviation is
meaningful only for the last two states and is rounded down for display;
classification compares before rounding. `SCENE_CLOCK_TSC_TOLERANCE_PPM` is
50,000 ppm (5%), relative to the reported frequency. This status is diagnostic
only and never changes clock selection.

The value is not a wall clock and contains no date or timezone. Only a 64-bit
HPET main counter is currently accepted. On a non-invariant TSC, the value only
promises non-decreasing readings during the current BSP run, not long-term
rate accuracy.
The invariant check in `scene_clock_source_stable()` and frequency comparison
describe different properties; one boolean cannot replace the full snapshot.
TSC measurement failure does not fail initialization when HPET is available.

## Local hardware timer

The header is `scene/local_timer.h`. The current xAPIC evaluation path covers
only the BSP. Calls are restricted to serial ordinary kernel context on one
CPU. This is not a stable ABI and accepts no user callbacks.

- `scene_local_timer_init(calibration_ticks)` calibrates once over 2–16 PIT
  ticks, requiring a ready clock/PIT and IF enabled. HPET measures elapsed
  time when available; otherwise PIT supplies the reference. Inactive APIC,
  repeated initialization, invalid parameters/measurement, and watchdog
  timeout return `false`.
- `scene_local_timer_ready()` returns `true` after calibration; stop retains it.
- `scene_local_timer_start_one_shot(delay_ns)` rounds delay upward to a
  nonzero hardware count. Zero delay, a reload above 32 bits, or another invalid
  request returns `false`.
- `scene_local_timer_start_periodic(frequency_hz)` uses the nearest integer
  reload. Frequency must be nonzero and no greater than the divided counter
  rate; the actual rate may differ from the request.
- `scene_local_timer_stop()` stops counting, masks interrupts, and sets stopped
  mode. Repeated calls are allowed.
- `scene_local_timer_get_info(out)` copies a consistent snapshot. An
  uncalibrated timer or null pointer returns `false` without writing the
  destination. The caller owns the snapshot.

Both start calls require calibration, stopped mode, and no old interrupt
awaiting service. Failure never replaces an active timer. An interrupt raised
before stop may still arrive; stopped state ignores it for event counting but
still receives EOI. If that temporarily prevents rearm, allow interrupts to
drain before retrying; never retry indefinitely with IF cleared. Acceptance of
a one-shot interrupt automatically returns mode to `SCENE_LOCAL_TIMER_STOPPED`.
Configuration, stop, and snapshot access preserve the caller's IF state. These
local critical sections do not provide SMP/NMI safety.

`scene_local_timer_info` fields:

| Field | Meaning |
| --- | --- |
| `counter_frequency_hz` | Calibrated down-counter rate after divide by 16, from 1 through UINT32_MAX Hz |
| `interrupts` | Cumulative interrupts accepted while active; excludes late stopped events and is not reset on rearm |
| `reload_count` | Last successful start's reload, retained after stop; not a live remaining count |
| `mode` | `SCENE_LOCAL_TIMER_STOPPED`, `ONE_SHOT`, or `PERIODIC` with the same enum prefix |
| `calibrated_with_hpet` | `true` for an HPET reference, `false` for PIT |

Event counts are not a monotonic clock. Periodic interrupts need not record
every hardware expiration because pending events coalesce while interrupts
are blocked. This interface neither dispatches the software queue nor replaces
`scene_timer_ticks()`, and promises no cross-sleep or cross-CPU rate stability.

`scene_arch_local_timer_*` is the architecture adapter: `available` queries the
hardware path, `pending` checks pending/in-service events, `configure` programs
the counter, `current_count` reads its remainder, and `stop` stops it. The
local-timer core calls configuration/stop with local interrupts disabled.
External drivers must not bypass the core's hardware/mode state. The
architecture entry calls internal `on_interrupt`, then sends EOI centrally.

## Timer queue

- `scene_timer_schedule_at(deadline_ns, callback, context, id)` schedules an
  absolute monotonic deadline.
- `scene_timer_schedule_after(delay_ns, callback, context, id)` schedules a
  relative delay.
- `scene_timer_cancel(id)` removes an item that is still queued.
- `scene_timer_run_due()` invokes all due callbacks in the caller's context
  and returns their count.
- `scene_timer_pending()` returns the number of waiting items.

`SCENE_TIMER_QUEUE_CAPACITY` fixes capacity at 32. Equal deadlines are stably
ordered by increasing timer ID. The queue is not concurrency-safe yet.
Callbacks may schedule more items, but must not assume hardware-interrupt
context.
