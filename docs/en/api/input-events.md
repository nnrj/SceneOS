# Input event interfaces

The current kernel-private API is declared in `scene/input.h`.

- `scene_input_init()` resets queue state and sequence numbering. Call it once
  before input drivers attach.
- `scene_input_push_key(event)` validates and queues a semantic key event. It
  returns false for invalid input or overflow.
- `scene_input_pop(event)` removes the oldest event and returns false when the
  queue is empty or the destination is null.
- `scene_input_pending()` returns the current queued count.
- `scene_input_dropped()` returns the cumulative overflow count since init.

Every accepted event receives a monotonically increasing `sequence`, a type,
and a key payload. The payload contains a device-independent `scene_key_code`,
modifier bits, `pressed`, and `repeat`. A release has `pressed == false`;
repeat is only meaningful for pressed events. Key codes describe physical key
meaning, not text or Unicode.

`scene/ps2_keyboard.h` exposes initialization and a diagnostic received-byte
counter. Applications and future drivers should consume input events rather
than PS/2 scan codes. These are pre-stable kernel interfaces and may evolve
together with all in-tree callers.
