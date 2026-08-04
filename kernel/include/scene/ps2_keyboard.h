#ifndef SCENE_PS2_KEYBOARD_H
#define SCENE_PS2_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

bool scene_ps2_keyboard_init(void);
uint64_t scene_ps2_keyboard_scancodes(void);

#endif
