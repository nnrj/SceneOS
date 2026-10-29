#ifndef SCENE_CLOCK_H
#define SCENE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

struct scene_boot_info;

#define SCENE_CLOCK_TSC_TOLERANCE_PPM UINT64_C(50000)

enum scene_clock_tsc_check {
    SCENE_CLOCK_TSC_UNAVAILABLE = 0,
    SCENE_CLOCK_TSC_INVALID,
    SCENE_CLOCK_TSC_UNMEASURED,
    SCENE_CLOCK_TSC_MATCH,
    SCENE_CLOCK_TSC_MISMATCH
};

struct scene_clock_diagnostics {
    uint64_t tsc_reported_hz;
    uint64_t tsc_measured_hz;
    uint64_t tsc_deviation_ppm;
    bool tsc_invariant;
    enum scene_clock_tsc_check tsc_check;
};

bool scene_clock_init(
    const struct scene_boot_info *boot_info,
    uint64_t calibration_ticks
);
bool scene_clock_ready(void);
const char *scene_clock_source_name(void);
bool scene_clock_source_stable(void);
uint64_t scene_clock_source_frequency_hz(void);
uint64_t scene_clock_monotonic_ns(void);
bool scene_clock_get_diagnostics(struct scene_clock_diagnostics *diagnostics);
const char *scene_clock_tsc_check_name(void);

#endif
