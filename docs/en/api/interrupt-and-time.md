# Interrupt and time interfaces

These are kernel module interfaces, not a stable driver ABI.

## Interrupt control

- `scene_arch_interrupts_init()` installs IRQ IDT entries, remaps the 8259A,
  and masks it.
- `scene_arch_interrupts_activate_apic(boot_info)` discovers and enables the
  Local APIC and IOAPIC from ACPI MADT.
- `scene_arch_interrupts_enable()` / `disable()` control IF on the current CPU.
- `scene_arch_interrupt_controller()` returns a diagnostic controller name.

The required order is exception IDT, PIC-compatible initialization, paging
ownership, APIC activation, device timer initialization, and only then `sti`.
The current implementation covers the BSP and one IOAPIC as an early boot
configuration.

## Time interface

- `scene_timer_init(frequency_hz)` configures PIT channel 0 and routes IRQ0.
- `scene_timer_frequency()` returns the divisor-derived frequency, which may
  differ from the requested value.
- `scene_timer_ticks()` returns ticks observed since initialization.
- `scene_timer_wait_ticks(count)` waits for a relative number of ticks and
  returns `false` if its startup watchdog expires.

`scene_timer_ticks()` currently relies on aligned x86-64 64-bit access. It has
no SMP synchronization guarantee and is not a stable wall clock, a
timeout-safe source, or a user-space ABI. Drivers must not call
`scene_timer_handle_tick()` directly; that entry belongs to the IRQ dispatcher.

## Monotonic clock

- `scene_clock_init(calibration_ticks)` calibrates the architecture counter
  against PIT ticks.
- `scene_clock_ready()` reports whether calibration completed.
- `scene_clock_source_invariant()` reports whether the CPU advertises an
  invariant TSC.
- `scene_clock_source_frequency_hz()` returns the calibrated frequency.
- `scene_clock_monotonic_ns()` returns nanoseconds from the calibration origin.

The value is not a wall clock and contains no date or timezone. On a
non-invariant TSC, it only promises non-decreasing values during the current
BSP run, not long-term rate accuracy.

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
