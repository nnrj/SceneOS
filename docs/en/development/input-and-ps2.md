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

The driver deliberately does not produce characters. In ordinary consumer
context, `core/text_input.c` applies an explicit US layout to semantic presses,
producing `scene_text_event` with the source sequence and repeat flag. Letters
use Shift XOR Caps; only Shift affects digits/punctuation. Ctrl/Alt chords,
releases, and editing/navigation keys produce no text. Translation reads the
event's modifier snapshot without maintaining a second key-state tracker,
running inside IRQs, or adding a producer to the raw ring queue. The consumer
owns the resulting text.

The initial layout covers 26 letters, ten digits, US punctuation, and space,
emitting the printable ASCII subset of Unicode scalars. A separate UTF-8
encoder supports 1–4 byte scalar encoding and rejects surrogates, out-of-range
values, and insufficient buffers without partial writes. It follows
[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629) and performs no normalization,
UTF-8 decoding, or input-method processing. Unicode composition, dead keys,
AltGr, more layouts, and input methods remain future text-service work. PS/2
still lacks Pause/Print Screen sequences, LEDs, hot-plug recovery, and multiple
keyboards.

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

Text-path acceptance:

```powershell
python tools/scenectl.py smoke --text-input --timeout 15
python tools/scenectl.py smoke --text-input --pic --timeout 15
python tools/scenectl.py smoke --text-input --tsc-clock --timeout 15
python tools/scenectl.py smoke --preset release --text-input --timeout 15
```

A separate self-test unit first checks every letter/modifier combination,
symbols/Caps, repeat, non-text keys, and unchanged output on invalid arguments.
UTF-8 checks cover length boundaries, Chinese/supplementary-plane values,
surrogates, and short buffers. The runner then injects 15 key groups through
[QEMU Monitor](https://www.qemu.org/docs/master/system/monitor.html), including
left/right Shift, Caps toggles, Ctrl/Alt chords, space, Enter, Backspace, and an
extended arrow key. Each group must finish all presses/releases before the
guest emits the next readiness marker. The runner matches whole serial lines
before sending each command, so steps 1 and 10 cannot be confused.

Success requires `aAAa!b Z`, exactly eight text events, correct source sequences,
ordered raw events, and no queue loss. IRQ/device drivers still publish only
raw events; the test consumes them and calls the translator. Missing markers,
kernel faults, early process exit, or timeout fail the test and clean up its
QEMU process. Normal builds do not enable this test and still offer no visible
text-box or terminal input feedback.
