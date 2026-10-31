# Input event interfaces

The current kernel-private API is declared in `scene/input.h`.

- `scene_input_init()` resets queue state and sequence numbering. Call it once
  before input drivers attach.
- `scene_input_push_key(event)` validates and queues a semantic key event. It
  returns false for invalid input or overflow.
- `scene_input_push_pointer(event)` queues relative motion, scrolling, or button changes.
  It returns false for null, no-op, or overflowing events.
- `scene_input_pop(event)` removes the oldest event and returns false when the
  queue is empty or the destination is null.
- `scene_input_wait(event)` is the blocking alternative for the sole consumer,
  usable only in an initialized worker with IF set. It atomically pops or waits
  for an input IRQ wake. Invalid context/null output return false unchanged;
  success returns one raw event. No timeout, cancellation, or fan-out is provided.
- `scene_input_pending()` returns the current queued count.
- `scene_input_dropped()` returns the cumulative overflow count since init.

Every accepted event receives a monotonically increasing `sequence`, a type,
and the corresponding payload. A key payload contains a device-independent `scene_key_code`,
modifier bits, `pressed`, and `repeat`. A release has `pressed == false`;
repeat is only meaningful for pressed events. Key codes describe physical key
meaning, not text or Unicode.

A pointer payload contains signed `delta_x`, `delta_y`, and `wheel_delta`, plus
the current `buttons` mask and a `changed_buttons` mask. Positive X means right
and positive Y means down. Consumers detect presses and releases by combining
the current and changed masks. `wheel_delta` counts device detents, positive up
and negative down, rather than pixels or text lines. It is zero for three-byte
mice; IntelliMouse ID 3 publishes the fourth-byte wheel displacement. Scaling,
acceleration, and scrolling policy belong to the consumer.

The PS/2 headers expose initialization and diagnostic received counters.
Applications and future drivers should consume input events rather than PS/2
scan codes or packets. These are pre-stable kernel interfaces and may evolve
together with all in-tree callers.

`scene_ps2_mouse_init()` is boot-only with IF clear; repeat initialization or
enabled interrupts return false. It probes wheels and selects the actual
packet length; unknown IDs or communication failures do not guess a fallback.
`scene_ps2_mouse_has_wheel()` reports ID 3 after successful initialization and
is false beforehand. `scene_ps2_mouse_packets()` counts complete non-overflow
packets, including unqueued no-change packets. `scene_ps2_device_command()` and
`scene_ps2_device_read(device, value)` also require IF clear. Invalid devices,
null read destinations, and timeouts fail; failed reads preserve output. They
are not concurrent runtime transaction APIs. Button snapshots advance even if
the queue drops an event; change masks do not replay missing events.

## Key-to-text translation

`scene/text_input.h` defines a consumer-side translator. The caller obtains a
raw input event, handles shortcuts/editing actions, then derives text as needed.
The PS/2 driver, `scene_input_event` types, and queue capacity stay unchanged.
The caller owns derived text; it is never written back into the raw queue and
consumes no additional queue slot.

- `scene_text_input_translate(layout, input, text)` translates one press into
  at most one text event and returns `true` on success. Only
  `SCENE_KEYBOARD_LAYOUT_US` is supported. No text, invalid arguments, or an
  unsupported layout return `false` without modifying output. The function
  neither consumes the queue nor changes input nor retains modifier state.
- `scene_text_input_encode_utf8(codepoint, buffer, capacity)` encodes one
  Unicode scalar as the shortest 1–4 byte UTF-8 sequence and returns its byte
  count. An invalid scalar, null buffer, or insufficient capacity returns zero
  and leaves the entire buffer unchanged. No NUL terminator is appended.

`scene_text_event` contains:

| Field | Meaning |
| --- | --- |
| `source_sequence` | Exact copy of the raw input `sequence`, for association rather than a new event sequence |
| `codepoint` | One Unicode scalar; the initial US layout emits only printable ASCII |
| `repeat` | The original press's repeat flag; no separate repeat timer is created |

Letter case uses Shift XOR Caps Lock. Only Shift affects digits/punctuation;
space remains space. Repeated presses can produce text. Releases, modifiers,
function/navigation keys, Escape, Enter, Tab, Backspace, and Delete remain raw
key actions. Ctrl or Alt suppress text instead of producing control characters;
unknown modifier bits also suppress it. The translator uses the event's
combined left/right modifier snapshot, not current global key state.

Following [RFC 3629 §3](https://www.rfc-editor.org/rfc/rfc3629#section-3), the
encoder accepts U+0000–U+10FFFF except surrogate values U+D800–U+DFFF. U+0000
produces one zero byte; callers must use the returned length instead of treating
all output as NUL-terminated strings. Chinese and supplementary-plane scalars
can be encoded, but this does not provide an input method, fonts, UTF-8 decoding,
or text composition.

Normal boot has no text-consumer UI. Automated tests consume and verify text;
no interactive terminal or text box has been added. Layout selection, focus,
dead keys, AltGr, input methods, and cross-process routing remain future work.
