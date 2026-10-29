#include <scene/arch.h>
#include <scene/boot.h>
#include <scene/clock.h>
#include <scene/timer.h>

#include <stdbool.h>
#include <stdint.h>

#include "clock_math.h"

#define SCENE_NANOSECONDS_PER_SECOND UINT64_C(1000000000)

static uint64_t counter_origin;
static uint64_t counter_frequency_hz;
static bool using_hpet;
static bool clock_ready;
static struct scene_clock_diagnostics clock_diagnostics;

static uint64_t read_counter(void) {
    return using_hpet
        ? scene_arch_hpet_counter()
        : scene_arch_timestamp_counter();
}

static bool calibrate_tsc(
    uint64_t calibration_ticks,
    uint64_t *frequency_hz,
    uint64_t *origin
) {
    const uint64_t timer_frequency = scene_timer_frequency();
    if (calibration_ticks < 2U || timer_frequency == 0 ||
        calibration_ticks > UINT64_C(1000) ||
        !scene_timer_wait_ticks(1U)) {
        return false;
    }

    const uint64_t initial_tick = scene_timer_ticks();
    const uint64_t initial_counter = scene_arch_timestamp_counter();
    if (!scene_timer_wait_ticks(calibration_ticks)) {
        return false;
    }
    const uint64_t final_counter = scene_arch_timestamp_counter();
    const uint64_t elapsed_ticks = scene_timer_ticks() - initial_tick;
    const uint64_t elapsed_counter = final_counter - initial_counter;
    if (elapsed_ticks == 0 ||
        elapsed_counter > UINT64_MAX / timer_frequency) {
        return false;
    }

    const uint64_t frequency =
        elapsed_counter * timer_frequency / elapsed_ticks;
    if (frequency < SCENE_MINIMUM_TSC_HZ ||
        frequency > SCENE_MAXIMUM_TSC_HZ) {
        return false;
    }

    *origin = final_counter;
    *frequency_hz = frequency;
    return true;
}

bool scene_clock_init(
    const struct scene_boot_info *boot_info,
    uint64_t calibration_ticks
) {
    clock_ready = false;
    using_hpet = false;
    counter_origin = 0;
    counter_frequency_hz = 0;
    clock_diagnostics = (struct scene_clock_diagnostics){
        .tsc_reported_hz = scene_arch_timestamp_counter_frequency_hz(),
        .tsc_invariant = scene_arch_timestamp_counter_invariant()
    };

#if !defined(SCENEOS_FORCE_TSC_CLOCK)
    if (scene_arch_hpet_init(boot_info)) {
        const uint64_t frequency = scene_arch_hpet_frequency_hz();
        if (frequency != 0 && frequency <= SCENE_MAXIMUM_TSC_HZ) {
            using_hpet = true;
            counter_frequency_hz = frequency;
        }
    }
#else
    (void)boot_info;
#endif

    uint64_t tsc_origin = 0;
    const bool calibrated = calibrate_tsc(
        calibration_ticks, &clock_diagnostics.tsc_measured_hz, &tsc_origin
    );
    scene_clock_evaluate_tsc(&clock_diagnostics);
    if (using_hpet) {
        /* A failed diagnostic must not disable a working HPET source. */
        counter_origin = scene_arch_hpet_counter();
    } else {
        if (!calibrated) {
            return false;
        }
        counter_origin = tsc_origin;
        /* CPUID is diagnostic only; keep the measured fallback frequency. */
        counter_frequency_hz = clock_diagnostics.tsc_measured_hz;
    }
    clock_ready = true;
    return true;
}

bool scene_clock_ready(void) {
    return clock_ready;
}

const char *scene_clock_source_name(void) {
    return using_hpet ? "HPET" : "calibrated TSC";
}

bool scene_clock_source_stable(void) {
    return using_hpet || clock_diagnostics.tsc_invariant;
}

uint64_t scene_clock_source_frequency_hz(void) {
    return counter_frequency_hz;
}

bool scene_clock_get_diagnostics(struct scene_clock_diagnostics *diagnostics) {
    if (!clock_ready || diagnostics == 0) {
        return false;
    }
    *diagnostics = clock_diagnostics;
    return true;
}

const char *scene_clock_tsc_check_name(void) {
    switch (clock_diagnostics.tsc_check) {
        case SCENE_CLOCK_TSC_UNAVAILABLE: return "unavailable";
        case SCENE_CLOCK_TSC_INVALID: return "invalid frequency";
        case SCENE_CLOCK_TSC_UNMEASURED: return "unmeasured";
        case SCENE_CLOCK_TSC_MATCH: return "within tolerance";
        case SCENE_CLOCK_TSC_MISMATCH: return "mismatch";
    }
    return "unknown";
}

uint64_t scene_clock_monotonic_ns(void) {
    if (!clock_ready) {
        return 0;
    }

    const uint64_t elapsed =
        read_counter() - counter_origin;
    const uint64_t seconds = elapsed / counter_frequency_hz;
    const uint64_t remainder = elapsed % counter_frequency_hz;
    if (seconds > UINT64_MAX / SCENE_NANOSECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    return seconds * SCENE_NANOSECONDS_PER_SECOND +
        remainder * SCENE_NANOSECONDS_PER_SECOND /
            counter_frequency_hz;
}
