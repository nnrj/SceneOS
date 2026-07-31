#ifndef SCENE_DESCRIPTORS_H
#define SCENE_DESCRIPTORS_H

#include <stdint.h>

void scene_arch_descriptors_init(void);
uint8_t scene_arch_ist_index(uintptr_t stack_address);

#endif
