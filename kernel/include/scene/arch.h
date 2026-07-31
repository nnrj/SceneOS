#ifndef SCENE_ARCH_H
#define SCENE_ARCH_H

#include <stdbool.h>
#include <stdint.h>

uint64_t scene_arch_timestamp_counter(void);
bool scene_arch_timestamp_counter_invariant(void);
_Noreturn void scene_arch_halt_forever(void);

#endif
