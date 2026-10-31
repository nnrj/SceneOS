#ifndef SCENE_CORE_LOCAL_TIMER_MATH_H
#define SCENE_CORE_LOCAL_TIMER_MATH_H

#include <stdbool.h>
#include <stdint.h>

#define SCENE_LOCAL_TIMER_NS_PER_SECOND UINT64_C(1000000000)

static inline bool scene_local_timer_measure_frequency(
    uint32_t initial_count, uint32_t final_count,
    uint64_t reference_ticks, uint64_t reference_hz, uint64_t *frequency
) {
    if (frequency == 0 || final_count == 0 || final_count >= initial_count ||
        reference_ticks == 0 || reference_hz == 0) {
        return false;
    }
    const uint64_t elapsed = (uint64_t)initial_count - final_count;
    if (elapsed > UINT64_MAX / reference_hz) {
        return false;
    }
    const uint64_t measured = elapsed * reference_hz / reference_ticks;
    if (measured == 0 || measured > UINT32_MAX) {
        return false;
    }
    *frequency = measured;
    return true;
}

static inline bool scene_local_timer_one_shot_count(
    uint64_t frequency, uint64_t delay_ns, uint32_t *count
) {
    if (count == 0 || frequency == 0 || frequency > UINT32_MAX ||
        delay_ns == 0) {
        return false;
    }
    const uint64_t seconds = delay_ns / SCENE_LOCAL_TIMER_NS_PER_SECOND;
    const uint64_t remainder = delay_ns % SCENE_LOCAL_TIMER_NS_PER_SECOND;
    if (seconds > UINT32_MAX / frequency) {
        return false;
    }
    /* Round upward: a sub-count request must not be programmed as zero. */
    const uint64_t result = seconds * frequency +
        (remainder * frequency + SCENE_LOCAL_TIMER_NS_PER_SECOND - 1U) /
            SCENE_LOCAL_TIMER_NS_PER_SECOND;
    if (result == 0 || result > UINT32_MAX) {
        return false;
    }
    *count = (uint32_t)result;
    return true;
}

static inline bool scene_local_timer_periodic_count(
    uint64_t frequency, uint32_t requested_hz, uint32_t *count
) {
    if (count == 0 || frequency == 0 || frequency > UINT32_MAX ||
        requested_hz == 0 || requested_hz > frequency) {
        return false;
    }
    const uint64_t result = (frequency + requested_hz / 2U) / requested_hz;
    if (result == 0 || result > UINT32_MAX) {
        return false;
    }
    *count = (uint32_t)result;
    return true;
}

#endif
