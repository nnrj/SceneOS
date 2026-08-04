#include <scene/input.h>
#include <scene/irq.h>
#include <scene/ps2_controller.h>
#include <scene/ps2_mouse.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_PS2_MOUSE_IRQ 12U
#define SCENE_PS2_MOUSE_SET_DEFAULTS 0xf6U
#define SCENE_PS2_MOUSE_ENABLE_STREAMING 0xf4U
#define SCENE_PS2_MOUSE_PACKET_SIZE 3U
#define SCENE_PS2_MOUSE_ALWAYS_ONE (1U << 3U)
#define SCENE_PS2_MOUSE_X_OVERFLOW (1U << 6U)
#define SCENE_PS2_MOUSE_Y_OVERFLOW (1U << 7U)

static uint8_t packet[SCENE_PS2_MOUSE_PACKET_SIZE];
static size_t packet_index;
static uint8_t current_buttons;
static volatile uint64_t received_packets;

static void publish_packet(void) {
    const uint8_t flags = packet[0];
    packet_index = 0;
    if ((flags & (SCENE_PS2_MOUSE_X_OVERFLOW |
        SCENE_PS2_MOUSE_Y_OVERFLOW)) != 0) {
        return;
    }

    const uint8_t buttons = flags & (
        SCENE_POINTER_BUTTON_LEFT |
        SCENE_POINTER_BUTTON_RIGHT |
        SCENE_POINTER_BUTTON_MIDDLE
    );
    const struct scene_pointer_event event = {
        .delta_x = (int32_t)(int8_t)packet[1],
        .delta_y = -(int32_t)(int8_t)packet[2],
        .wheel_delta = 0,
        .buttons = buttons,
        .changed_buttons = buttons ^ current_buttons
    };
    current_buttons = buttons;
    ++received_packets;
    (void)scene_input_push_pointer(&event);
}

static void handle_byte(uint8_t value) {
    if (packet_index == 0 &&
        (value & SCENE_PS2_MOUSE_ALWAYS_ONE) == 0) {
        return;
    }
    packet[packet_index] = value;
    ++packet_index;
    if (packet_index == SCENE_PS2_MOUSE_PACKET_SIZE) {
        publish_packet();
    }
}

static void mouse_irq(uint32_t irq, void *context) {
    (void)irq;
    (void)context;
    for (size_t count = 0; count < 16U; ++count) {
        const uint8_t status = scene_ps2_status();
        if ((status & SCENE_PS2_STATUS_OUTPUT_FULL) == 0 ||
            (status & SCENE_PS2_STATUS_AUXILIARY) == 0) {
            return;
        }
        handle_byte(scene_ps2_read_data());
    }
}

bool scene_ps2_mouse_init(void) {
    packet_index = 0;
    current_buttons = 0;
    received_packets = 0;
    if (!scene_ps2_device_command(
            SCENE_PS2_DEVICE_MOUSE,
            SCENE_PS2_MOUSE_SET_DEFAULTS
        ) ||
        !scene_ps2_device_command(
            SCENE_PS2_DEVICE_MOUSE,
            SCENE_PS2_MOUSE_ENABLE_STREAMING
        )) {
        return false;
    }
    return scene_irq_attach(SCENE_PS2_MOUSE_IRQ, mouse_irq, 0);
}

uint64_t scene_ps2_mouse_packets(void) {
    return received_packets;
}
