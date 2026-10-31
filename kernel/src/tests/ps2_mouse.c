#include "ps2_mouse.h"
#include "../drivers/input/ps2_mouse_protocol.h"

#include <scene/arch.h>
#include <scene/clock.h>
#include <scene/input.h>
#include <scene/log.h>
#include <scene/ps2_controller.h>
#include <scene/ps2_mouse.h>

struct scripted_mouse {
    const uint8_t *commands;
    size_t count;
    size_t position;
    size_t fail_at;
    uint8_t device_id;
    bool mismatch;
};

static bool scripted_command(void *context, uint8_t value) {
    struct scripted_mouse *mouse = context;
    const size_t position = mouse->position++;
    if (position >= mouse->count || value != mouse->commands[position]) {
        mouse->mismatch = true;
        return false;
    }
    return position != mouse->fail_at;
}

static bool scripted_read(void *context, uint8_t *value) {
    struct scripted_mouse *mouse = context;
    const size_t position = mouse->position++;
    if (position != mouse->count) {
        mouse->mismatch = true;
        return false;
    }
    if (position == mouse->fail_at) {
        return false;
    }
    *value = mouse->device_id;
    return true;
}

static bool verify_negotiation(void) {
    static const uint8_t probe[] = {0xf6, 0xf3, 200, 0xf3, 100, 0xf3, 80, 0xf2};
    static const uint8_t basic[] = {0xf6, 0xf2};
    static const uint8_t ids[] = {0, 3, 4, 255};
    for (size_t mode = 0; mode < 2U; ++mode) {
        const bool wheel = mode != 0;
        const size_t count = wheel ? sizeof(probe) : sizeof(basic);
        for (size_t index = 0; index < sizeof(ids); ++index) {
            /* Every command/argument ACK failure and the ID read failure must
             * stop the transaction without publishing a guessed protocol. */
            for (size_t failure = 0; failure <= count + 1U; ++failure) {
                struct scripted_mouse mouse = {
                    .commands = wheel ? probe : basic,
                    .count = count,
                    .fail_at = failure,
                    .device_id = ids[index]
                };
                const struct scene_ps2_mouse_transport transport = {
                    .command = scripted_command,
                    .read = scripted_read,
                    .context = &mouse
                };
                uint8_t result = 0xa5;
                const bool success = failure > count &&
                    (ids[index] == 0U || (wheel && ids[index] == 3U));
                if (scene_ps2_mouse_negotiate(&transport, wheel, &result) != success ||
                    mouse.mismatch || result != (success ? ids[index] : 0xa5U) ||
                    mouse.position != (failure <= count ? failure + 1U : count + 1U)) {
                    return false;
                }
            }
        }
    }
    uint8_t result = 0xa5;
    const struct scene_ps2_mouse_transport invalid = {0};
    return !scene_ps2_mouse_negotiate(0, true, &result) &&
        !scene_ps2_mouse_negotiate(&invalid, true, &result) &&
        !scene_ps2_mouse_negotiate(&invalid, true, 0) && result == 0xa5U;
}

static bool equal_event(
    const struct scene_pointer_event *left,
    const struct scene_pointer_event *right
) {
    return left->delta_x == right->delta_x && left->delta_y == right->delta_y &&
        left->wheel_delta == right->wheel_delta && left->buttons == right->buttons &&
        left->changed_buttons == right->changed_buttons;
}

static bool verify_packet(
    struct scene_ps2_mouse_decoder *decoder,
    const uint8_t bytes[4],
    const struct scene_pointer_event *expected
) {
    const struct scene_pointer_event sentinel = {71, 72, 73, 74, 75};
    struct scene_pointer_event result = sentinel;
    for (size_t index = 0; index < decoder->length; ++index) {
        const bool complete = expected != 0 && index + 1U == decoder->length;
        if (scene_ps2_mouse_decode(decoder, bytes[index], &result) != complete ||
            !equal_event(&result, complete ? expected : &sentinel)) {
            return false;
        }
    }
    return decoder->index == 0;
}

static bool verify_decoder(void) {
    for (size_t mode = 0; mode < 2U; ++mode) {
        struct scene_ps2_mouse_decoder decoder;
        const uint8_t id = mode != 0 ? SCENE_PS2_MOUSE_ID_WHEEL : SCENE_PS2_MOUSE_ID_BASIC;
        if (!scene_ps2_mouse_decoder_init(&decoder, id)) {
            return false;
        }
        /* Cover all nine-bit X/Y values, with independent header sign bits. */
        for (int32_t displacement = -256; displacement <= 255; ++displacement) {
            for (size_t axis = 0; axis < 2U; ++axis) {
                uint8_t packet[4] = {0x08, 0, 0, 0};
                struct scene_pointer_event expected = {0};
                packet[axis + 1U] = (uint8_t)displacement;
                if (displacement < 0) {
                    packet[0] |= axis == 0 ? 0x10U : 0x20U;
                }
                if (axis == 0) {
                    expected.delta_x = displacement;
                } else {
                    expected.delta_y = -displacement;
                }
                if (!verify_packet(&decoder, packet, &expected)) {
                    return false;
                }
            }
        }
        if (mode != 0) {
            for (int32_t wheel = -128; wheel <= 127; ++wheel) {
                const uint8_t packet[] = {0x08, 0, 0, (uint8_t)wheel};
                const struct scene_pointer_event expected = {.wheel_delta = -wheel};
                if (!verify_packet(&decoder, packet, &expected)) {
                    return false;
                }
            }
        }
        /* Every button transition, repeated state, and overflow rejection. */
        uint8_t previous = 0;
        for (uint8_t from = 0; from < 8U; ++from) {
            for (uint8_t to = 0; to < 8U; ++to) {
                const uint8_t states[] = {from, to, to};
                for (size_t index = 0; index < sizeof(states); ++index) {
                    const uint8_t buttons = states[index];
                    uint8_t packet[] = {(uint8_t)(0x08U | buttons), 0, 0, 0};
                    const struct scene_pointer_event expected = {
                        .buttons = buttons, .changed_buttons = buttons ^ previous
                    };
                    if (!verify_packet(&decoder, packet, &expected)) {
                        return false;
                    }
                    previous = buttons;
                    packet[0] = (uint8_t)(0xc8U | (buttons ^ 7U));
                    if (!verify_packet(&decoder, packet, 0) || decoder.buttons != buttons) {
                        return false;
                    }
                }
            }
        }
        /* Discard bad leading bytes; a reset drops a partial old-mode packet. */
        struct scene_pointer_event result = {0};
        if (scene_ps2_mouse_decode(&decoder, 0, &result) || decoder.index != 0 ||
            scene_ps2_mouse_decode(&decoder, 0x08, 0) || decoder.index != 0 ||
            scene_ps2_mouse_decode(&decoder, 0x08, &result) || decoder.index != 1U ||
            scene_ps2_mouse_decoder_init(&decoder, 4U) || decoder.index != 1U ||
            !scene_ps2_mouse_decoder_init(&decoder, id) || decoder.index != 0 ||
            decoder.buttons != 0) {
            return false;
        }
        const uint8_t packet[] = {0x38, 0xff, 0xff, 0xff};
        const struct scene_pointer_event expected = {-1, 1, mode != 0 ? 1 : 0, 0, 0};
        if (!verify_packet(&decoder, packet, &expected)) {
            return false;
        }
    }
    struct scene_pointer_event result = {0};
    struct scene_ps2_mouse_decoder invalid = {0};
    return !scene_ps2_mouse_decoder_init(0, 0) &&
        !scene_ps2_mouse_decode(0, 0, &result) &&
        !scene_ps2_mouse_decode(&invalid, 0x08, &result);
}

static bool verify_injected_pointer(void) {
    /* Keep commands in scenectl.py in sync. The basic path omits wheel steps. */
    static const struct scene_pointer_event steps[] = {
        {12, 7, 0, 0, 0},
        {-9, -4, 0, 0, 0},
        {0, 0, 0, 1, 1},
        {0, 0, 0, 3, 2},
        {0, 0, 0, 7, 4},
        {0, 0, 0, 0, 7},
        {6, -3, 0, 0, 0},
        {0, 0, 1, 0, 0},
        {0, 0, -1, 0, 0}
    };
    const size_t count = scene_ps2_mouse_has_wheel() ? 9U : 7U;
    uint64_t last_sequence = 0;
    for (size_t step = 0; step < count; ++step) {
        if (scene_input_pending() != 0) {
            return false;
        }
        bool complete = false;
        const uint64_t start = scene_clock_monotonic_ns();
        scene_log_u64("Waiting for mouse protocol step: ", step);
        while (scene_clock_monotonic_ns() - start < UINT64_C(2000000000)) {
            struct scene_input_event input;
            if (scene_input_pop(&input)) {
                if (input.type != SCENE_INPUT_EVENT_POINTER ||
                    input.sequence <= last_sequence ||
                    !equal_event(&input.pointer, &steps[step])) {
                    scene_log_u64("Mouse protocol mismatch step: ", step);
                    scene_log_hex("Pointer X (signed): ", (uint64_t)(int64_t)input.pointer.delta_x);
                    scene_log_hex("Pointer Y (signed): ", (uint64_t)(int64_t)input.pointer.delta_y);
                    scene_log_hex("Pointer wheel (signed): ", (uint64_t)(int64_t)input.pointer.wheel_delta);
                    scene_log_u64("Pointer buttons: ", input.pointer.buttons);
                    scene_log_u64("Pointer changed buttons: ", input.pointer.changed_buttons);
                    return false;
                }
                last_sequence = input.sequence;
                complete = true;
                break;
            }
        }
        if (!complete) {
            return false;
        }
    }
    scene_log_u64("PS/2 pointer event count: ", count);
    scene_log_u64("PS/2 mouse packets received: ", scene_ps2_mouse_packets());
    scene_log_u64("Input events dropped: ", scene_input_dropped());
    return scene_input_dropped() == 0 && scene_input_pending() == 0;
}

bool scene_ps2_mouse_self_test(void) {
    if (!verify_negotiation()) {
        return false;
    }
    scene_log_line("PS/2 mouse negotiation/fallback failures: verified");
    if (!verify_decoder()) {
        return false;
    }
    scene_log_line("PS/2 mouse packet/sign/button boundaries: verified");
    uint8_t response = 0xa5;
    if (!scene_arch_interrupts_enabled() ||
        scene_ps2_device_command(SCENE_PS2_DEVICE_MOUSE, 0xf6U) ||
        scene_ps2_device_read(SCENE_PS2_DEVICE_MOUSE, &response) || response != 0xa5U) {
        return false;
    }
    scene_log_line("PS/2 runtime polling rejection: verified");
    if (!verify_injected_pointer()) {
        return false;
    }
    scene_log_line(scene_ps2_mouse_has_wheel()
        ? "PS/2 wheel/motion/buttons: verified"
        : "PS/2 basic motion/buttons: verified");
    return true;
}
