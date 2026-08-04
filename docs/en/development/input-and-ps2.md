# Input core and PS/2 devices

The input core separates device bytes from consumers. `input.c` owns a
64-slot circular queue of `scene_input_event` values. Keyboard IRQ1 and mouse
IRQ12 run as one serialized interrupt-producer context on the BSP; bootstrap
kernel code is the consumer. One slot is reserved to distinguish full from
empty, so at most 63 events are pending. Overflow drops the new event and
increments a visible counter.

This first implementation assumes non-nested maskable interrupt handlers, one
consumer, one BSP, and x86's ordered memory model. Although two devices publish
events, the interrupt gate serializes their producer execution. It is not an
SMP-safe general-purpose queue. Before adding other CPUs, producers, or
user-space consumers, introduce explicit atomic ordering and a fan-out or
focus-routing policy.

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
also include no Pause/Print Screen sequences, LEDs, hot-plug recovery, or
multiple keyboards.

The shared controller layer enables both i8042 ports and verifies whether each
command response came from the requested device. The mouse enables defaults
and streaming, assembles synchronized three-byte packets on IRQ12, rejects
overflow packets, converts PS/2 Y-up motion to screen-style Y-down motion, and
publishes relative deltas plus current/changed left, right, and middle buttons.
Wheel negotiation and four-byte packets are not implemented yet.

Use `python tools/scenectl.py smoke --keyboard --timeout 10` to build the
self-test kernel and inject `A` with QEMU Monitor. Success proves the emulator
device, IRQ1 route, decoder, and queue as one path.

Use `python tools/scenectl.py smoke --mouse --timeout 10` to inject relative
motion and prove the second controller port, IRQ12 route, packet decoder, and
pointer-event path.
