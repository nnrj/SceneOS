#include <scene/arch.h>
#include <scene/input.h>
#include <scene/irq.h>
#include <scene/ps2_controller.h>
#include <scene/ps2_mouse.h>

#include "ps2_mouse_protocol.h"

#define SCENE_PS2_MOUSE_IRQ 12U
#define SCENE_PS2_MOUSE_ENABLE_STREAMING 0xf4U

static struct scene_ps2_mouse_decoder decoder;
static bool initialized;
static bool has_wheel;
static volatile uint64_t received_packets;

static bool mouse_command(void *context, uint8_t value) {
    (void)context;
    return scene_ps2_device_command(SCENE_PS2_DEVICE_MOUSE, value);
}

static bool mouse_read(void *context, uint8_t *value) {
    (void)context;
    return scene_ps2_device_read(SCENE_PS2_DEVICE_MOUSE, value);
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
        struct scene_pointer_event event;
        if (scene_ps2_mouse_decode(&decoder, scene_ps2_read_data(), &event)) {
            ++received_packets;
            (void)scene_input_push_pointer(&event);
        }
    }
}

bool scene_ps2_mouse_init(void) {
    if (initialized || scene_arch_interrupts_enabled()) {
        return false;
    }
    const struct scene_ps2_mouse_transport transport = {
        .command = mouse_command,
        .read = mouse_read,
        .context = 0
    };
#if defined(SCENEOS_FORCE_BASIC_MOUSE)
    const bool probe_wheel = false;
#else
    const bool probe_wheel = true;
#endif
    uint8_t device_id;
    if (!scene_ps2_mouse_negotiate(&transport, probe_wheel, &device_id) ||
        !scene_ps2_mouse_decoder_init(&decoder, device_id) ||
        !scene_irq_attach(SCENE_PS2_MOUSE_IRQ, mouse_irq, 0)) {
        return false;
    }
    if (!mouse_command(0, SCENE_PS2_MOUSE_ENABLE_STREAMING)) {
        (void)scene_irq_detach(SCENE_PS2_MOUSE_IRQ, mouse_irq, 0);
        return false;
    }
    received_packets = 0;
    has_wheel = device_id == SCENE_PS2_MOUSE_ID_WHEEL;
    initialized = true;
    return true;
}

bool scene_ps2_mouse_has_wheel(void) {
    return has_wheel;
}

uint64_t scene_ps2_mouse_packets(void) {
    return received_packets;
}
