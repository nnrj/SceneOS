# Input core and PS/2 keyboard

The input core separates device bytes from consumers. `input.c` owns a
64-slot circular queue of `scene_input_event` values; the initial producer is
the keyboard IRQ handler and the consumer is bootstrap kernel code. One slot
is reserved to distinguish full from empty, so at most 63 events are pending.
Overflow drops the new event and increments a visible counter.

This first implementation assumes one producer, one consumer, one BSP, and
x86's ordered memory model. It is not an SMP-safe general-purpose queue.
Before adding other producers or user-space consumers, introduce explicit
atomic ordering and a fan-out or focus-routing policy.

The i8042 driver disables the first port while configuring it, flushes stale
output, enables first-port interrupts and translation, enables scanning, then
attaches IRQ1 through the core IRQ registry. Its bounded polling loops fail
initialization instead of hanging forever. The interrupt handler drains up to
16 keyboard bytes, ignores auxiliary-device data, decodes common Set 1 and
extended keys, tracks left/right modifiers and Caps Lock, and publishes key
press, release, and repeat events.

The driver deliberately does not produce characters. Keyboard layout,
Shift/Caps behavior, Unicode composition, dead keys, and input methods belong
to a later text-input service above semantic key events. Current limitations
also include no Pause/Print Screen sequences, LEDs, hot-plug recovery, mouse,
or multiple keyboards.

Use `python tools/scenectl.py smoke --keyboard --timeout 10` to build the
self-test kernel and inject `A` with QEMU Monitor. Success proves the emulator
device, IRQ1 route, decoder, and queue as one path.
