#ifndef SCENE_PS2_MOUSE_PROTOCOL_H
#define SCENE_PS2_MOUSE_PROTOCOL_H

#include <scene/input.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_PS2_MOUSE_ID_BASIC 0U
#define SCENE_PS2_MOUSE_ID_WHEEL 3U

/* Private protocol layer. Commands include ACK handling in the transport;
 * read consumes the following data byte, never an ACK. */
struct scene_ps2_mouse_transport {
    bool (*command)(void *context, uint8_t value);
    bool (*read)(void *context, uint8_t *value);
    void *context;
};

struct scene_ps2_mouse_decoder {
    uint8_t packet[4];
    size_t index;
    size_t length;
    uint8_t buttons;
};

bool scene_ps2_mouse_negotiate(
    const struct scene_ps2_mouse_transport *transport,
    bool probe_wheel,
    uint8_t *device_id
);
bool scene_ps2_mouse_decoder_init(
    struct scene_ps2_mouse_decoder *decoder, uint8_t device_id
);
/* True only for a complete, non-overflow packet, including no-change packets.
 * Other returns preserve event. State carries across IRQ byte batches. */
bool scene_ps2_mouse_decode(
    struct scene_ps2_mouse_decoder *decoder,
    uint8_t value,
    struct scene_pointer_event *event
);

#endif
