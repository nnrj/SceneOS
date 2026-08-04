#ifndef SCENE_TIMER_H
#define SCENE_TIMER_H

#include <stdbool.h>
#include <stdint.h>

bool scene_timer_init(uint32_t frequency_hz);
uint32_t scene_timer_frequency(void);
uint64_t scene_timer_ticks(void);
bool scene_timer_wait_ticks(uint64_t tick_count);

#endif
