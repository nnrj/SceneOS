#ifndef SCENE_PS2_CONTROLLER_H
#define SCENE_PS2_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#define SCENE_PS2_STATUS_OUTPUT_FULL (1U << 0U)
#define SCENE_PS2_STATUS_AUXILIARY (1U << 5U)

enum scene_ps2_device {
    SCENE_PS2_DEVICE_KEYBOARD,
    SCENE_PS2_DEVICE_MOUSE
};

bool scene_ps2_controller_init(void);
bool scene_ps2_device_command(
    enum scene_ps2_device device,
    uint8_t command
);
uint8_t scene_ps2_status(void);
uint8_t scene_ps2_read_data(void);

#endif
