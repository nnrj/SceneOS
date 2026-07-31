#ifndef SCENE_CLOCK_H
#define SCENE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

bool scene_clock_init(uint64_t calibration_ticks);
bool scene_clock_ready(void);
bool scene_clock_source_invariant(void);
uint64_t scene_clock_source_frequency_hz(void);
uint64_t scene_clock_monotonic_ns(void);

#endif
