#include <scene/arch.h>
#include <scene/boot.h>
#include <scene/clock.h>
#include <scene/timer.h>

#include <stdbool.h>
#include <stdint.h>

#define SCENE_NANOSECONDS_PER_SECOND UINT64_C(1000000000)
#define SCENE_MINIMUM_TSC_HZ UINT64_C(1000000)
#define SCENE_MAXIMUM_TSC_HZ UINT64_C(10000000000)

static uint64_t counter_origin;
static uint64_t counter_frequency_hz;
static bool using_hpet;
static bool clock_ready;

static uint64_t read_counter(void) {
    return using_hpet
        ? scene_arch_hpet_counter()
        : scene_arch_timestamp_counter();
}

bool scene_clock_init(
    const struct scene_boot_info *boot_info,
    uint64_t calibration_ticks
) {
#if !defined(SCENEOS_FORCE_TSC_CLOCK)
    if (scene_arch_hpet_init(boot_info)) {
        const uint64_t frequency = scene_arch_hpet_frequency_hz();
        if (frequency != 0 &&
            frequency <= SCENE_MAXIMUM_TSC_HZ) {
            using_hpet = true;
            counter_frequency_hz = frequency;
            counter_origin = scene_arch_hpet_counter();
            clock_ready = true;
            return true;
        }
    }
#else
    (void)boot_info;
#endif

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

    counter_origin = final_counter;
    counter_frequency_hz = frequency;
    using_hpet = false;
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
    return using_hpet || scene_arch_timestamp_counter_invariant();
}

uint64_t scene_clock_source_frequency_hz(void) {
    return counter_frequency_hz;
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
