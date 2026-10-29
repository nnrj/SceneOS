#ifndef SCENE_CORE_CLOCK_MATH_H
#define SCENE_CORE_CLOCK_MATH_H

#include <scene/clock.h>

#define SCENE_MINIMUM_TSC_HZ UINT64_C(1000000)
#define SCENE_MAXIMUM_TSC_HZ UINT64_C(10000000000)
#define SCENE_PARTS_PER_MILLION UINT64_C(1000000)

static inline void scene_clock_evaluate_tsc(
    struct scene_clock_diagnostics *diagnostics
) {
    const uint64_t reported = diagnostics->tsc_reported_hz;
    const uint64_t measured = diagnostics->tsc_measured_hz;
    diagnostics->tsc_deviation_ppm = 0;
    if (reported == 0) {
        diagnostics->tsc_check = SCENE_CLOCK_TSC_UNAVAILABLE;
    } else if (reported < SCENE_MINIMUM_TSC_HZ ||
        reported > SCENE_MAXIMUM_TSC_HZ ||
        (measured != 0 && (measured < SCENE_MINIMUM_TSC_HZ ||
            measured > SCENE_MAXIMUM_TSC_HZ))) {
        diagnostics->tsc_check = SCENE_CLOCK_TSC_INVALID;
    } else if (measured == 0) {
        diagnostics->tsc_check = SCENE_CLOCK_TSC_UNMEASURED;
    } else {
        const uint64_t difference = measured >= reported
            ? measured - reported : reported - measured;
        /* Supported frequency bounds keep both products within uint64_t. */
        const uint64_t scaled = difference * SCENE_PARTS_PER_MILLION;
        diagnostics->tsc_deviation_ppm = scaled / reported;
        /* Compare before rounding ppm so a just-outside value is rejected. */
        diagnostics->tsc_check =
            scaled <= reported * SCENE_CLOCK_TSC_TOLERANCE_PPM
                ? SCENE_CLOCK_TSC_MATCH : SCENE_CLOCK_TSC_MISMATCH;
    }
}

#endif
