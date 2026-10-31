#include "ps2_mouse_protocol.h"

#define SCENE_PS2_MOUSE_SET_DEFAULTS 0xf6U
#define SCENE_PS2_MOUSE_SET_RATE 0xf3U
#define SCENE_PS2_MOUSE_GET_ID 0xf2U
#define SCENE_PS2_MOUSE_PACKET_TAG (1U << 3U)
#define SCENE_PS2_MOUSE_X_SIGN (1U << 4U)
#define SCENE_PS2_MOUSE_Y_SIGN (1U << 5U)
#define SCENE_PS2_MOUSE_OVERFLOW ((1U << 6U) | (1U << 7U))

bool scene_ps2_mouse_negotiate(
    const struct scene_ps2_mouse_transport *transport,
    bool probe_wheel,
    uint8_t *device_id
) {
    if (transport == 0 || transport->command == 0 || transport->read == 0 ||
        device_id == 0) {
        return false;
    }
    /* Defaults disable streaming before the query. They do not necessarily
     * reset an already negotiated device ID, so always read the actual ID. */
    if (!transport->command(transport->context, SCENE_PS2_MOUSE_SET_DEFAULTS)) {
        return false;
    }
    if (probe_wheel) {
        static const uint8_t rates[] = {200U, 100U, 80U};
        for (size_t index = 0; index < sizeof(rates); ++index) {
            if (!transport->command(transport->context, SCENE_PS2_MOUSE_SET_RATE) ||
                !transport->command(transport->context, rates[index])) {
                return false;
            }
        }
    }
    uint8_t reported_id;
    if (!transport->command(transport->context, SCENE_PS2_MOUSE_GET_ID) ||
        !transport->read(transport->context, &reported_id)) {
        return false;
    }
    if (reported_id != SCENE_PS2_MOUSE_ID_BASIC &&
        (reported_id != SCENE_PS2_MOUSE_ID_WHEEL || !probe_wheel)) {
        return false;
    }
    *device_id = reported_id;
    return true;
}

bool scene_ps2_mouse_decoder_init(
    struct scene_ps2_mouse_decoder *decoder, uint8_t device_id
) {
    if (decoder == 0 || (device_id != SCENE_PS2_MOUSE_ID_BASIC &&
        device_id != SCENE_PS2_MOUSE_ID_WHEEL)) {
        return false;
    }
    *decoder = (struct scene_ps2_mouse_decoder){
        .length = device_id == SCENE_PS2_MOUSE_ID_WHEEL ? 4U : 3U
    };
    return true;
}

bool scene_ps2_mouse_decode(
    struct scene_ps2_mouse_decoder *decoder,
    uint8_t value,
    struct scene_pointer_event *event
) {
    if (decoder == 0 || event == 0 ||
        (decoder->length != 3U && decoder->length != 4U) ||
        decoder->index >= decoder->length) {
        return false;
    }
    if (decoder->index == 0 && (value & SCENE_PS2_MOUSE_PACKET_TAG) == 0) {
        return false;
    }
    decoder->packet[decoder->index++] = value;
    if (decoder->index != decoder->length) {
        return false;
    }
    decoder->index = 0;
    const uint8_t flags = decoder->packet[0];
    if ((flags & SCENE_PS2_MOUSE_OVERFLOW) != 0) {
        return false;
    }
    const uint8_t buttons = flags & (
        SCENE_POINTER_BUTTON_LEFT | SCENE_POINTER_BUTTON_RIGHT |
        SCENE_POINTER_BUTTON_MIDDLE
    );
    /* The header supplies the ninth bit of each X/Y displacement. */
    const int32_t x = (int32_t)decoder->packet[1] -
        ((flags & SCENE_PS2_MOUSE_X_SIGN) != 0 ? 256 : 0);
    const int32_t y = (int32_t)decoder->packet[2] -
        ((flags & SCENE_PS2_MOUSE_Y_SIGN) != 0 ? 256 : 0);
    int32_t wheel = 0;
    if (decoder->length == 4U) {
        wheel = (int32_t)decoder->packet[3] -
            ((decoder->packet[3] & 0x80U) != 0 ? 256 : 0);
    }
    *event = (struct scene_pointer_event){
        .delta_x = x,
        .delta_y = -y,
        .wheel_delta = -wheel,
        .buttons = buttons,
        .changed_buttons = buttons ^ decoder->buttons
    };
    decoder->buttons = buttons;
    return true;
}
