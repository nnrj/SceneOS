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
