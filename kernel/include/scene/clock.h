#ifndef SCENE_CLOCK_H
#define SCENE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

struct scene_boot_info;

bool scene_clock_init(
    const struct scene_boot_info *boot_info,
    uint64_t calibration_ticks
);
bool scene_clock_ready(void);
const char *scene_clock_source_name(void);
bool scene_clock_source_stable(void);
uint64_t scene_clock_source_frequency_hz(void);
uint64_t scene_clock_monotonic_ns(void);

#endif
