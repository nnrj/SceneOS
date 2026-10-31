#ifndef SCENE_PS2_MOUSE_H
#define SCENE_PS2_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

bool scene_ps2_mouse_init(void);
bool scene_ps2_mouse_has_wheel(void);
uint64_t scene_ps2_mouse_packets(void);

#endif
