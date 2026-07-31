#ifndef SCENE_SERIAL_H
#define SCENE_SERIAL_H

#include <stdbool.h>

bool scene_serial_init(void);
void scene_serial_write_char(char value);
void scene_serial_write(const char *text);

#endif

