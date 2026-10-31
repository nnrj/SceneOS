#include <scene/arch.h>
#include <scene/clock.h>
#include <scene/local_timer.h>
#include <scene/timer.h>

#include "local_timer_internal.h"
#include "local_timer_math.h"

static uint64_t counter_frequency;
static uint32_t last_reload;
static volatile uint64_t delivered_interrupts;
static volatile enum scene_local_timer_mode timer_mode;
static bool ready;
static bool calibrated_with_hpet;

static uint64_t reference_counter(bool hpet) {
    return hpet ? scene_arch_hpet_counter() : scene_timer_ticks();
}

bool scene_local_timer_init(uint64_t calibration_ticks) {
    if (ready || !scene_arch_local_timer_available() || !scene_clock_ready() ||
        !scene_arch_interrupts_enabled() || calibration_ticks < 2U ||
        calibration_ticks > 16U || !scene_timer_wait_ticks(1U)) {
        return false;
    }
    const bool hpet = scene_arch_hpet_ready();
    const uint64_t reference_hz = hpet
        ? scene_arch_hpet_frequency_hz() : scene_timer_frequency();
    uint64_t state = scene_arch_interrupts_save();
    const bool started = scene_arch_local_timer_configure(
        UINT32_MAX, false, true
    );
    const uint64_t reference_start = reference_counter(hpet);
    const uint32_t initial_count = scene_arch_local_timer_current_count();
    scene_arch_interrupts_restore(state);
    if (!started) {
        return false;
    }
    if (!scene_timer_wait_ticks(calibration_ticks)) {
        scene_local_timer_stop();
        return false;
    }
    state = scene_arch_interrupts_save();
    const uint32_t final_count = scene_arch_local_timer_current_count();
    const uint64_t reference_end = reference_counter(hpet);
    scene_arch_local_timer_stop();
    scene_arch_interrupts_restore(state);
    if (reference_end <= reference_start ||
        !scene_local_timer_measure_frequency(
            initial_count, final_count, reference_end - reference_start,
            reference_hz, &counter_frequency
        )) {
        return false;
    }
    calibrated_with_hpet = hpet;
    ready = true;
    return true;
}

bool scene_local_timer_ready(void) {
    return ready;
}

static bool arm_timer(uint32_t count, enum scene_local_timer_mode mode) {
    const uint64_t state = scene_arch_interrupts_save();
    if (!ready || timer_mode != SCENE_LOCAL_TIMER_STOPPED ||
        !scene_arch_local_timer_configure(
            count, mode == SCENE_LOCAL_TIMER_PERIODIC, false
        )) {
        scene_arch_interrupts_restore(state);
        return false;
    }
    last_reload = count;
    timer_mode = mode;
    scene_arch_interrupts_restore(state);
    return true;
}

bool scene_local_timer_start_one_shot(uint64_t delay_ns) {
    uint32_t count;
    return ready && scene_local_timer_one_shot_count(
        counter_frequency, delay_ns, &count
    ) && arm_timer(count, SCENE_LOCAL_TIMER_ONE_SHOT);
}

bool scene_local_timer_start_periodic(uint32_t frequency_hz) {
    uint32_t count;
    return ready && scene_local_timer_periodic_count(
        counter_frequency, frequency_hz, &count
    ) && arm_timer(count, SCENE_LOCAL_TIMER_PERIODIC);
}

void scene_local_timer_stop(void) {
    const uint64_t state = scene_arch_interrupts_save();
    timer_mode = SCENE_LOCAL_TIMER_STOPPED;
    scene_arch_local_timer_stop();
    scene_arch_interrupts_restore(state);
}

bool scene_local_timer_get_info(struct scene_local_timer_info *info) {
    if (!ready || info == 0) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    *info = (struct scene_local_timer_info){
        .counter_frequency_hz = counter_frequency,
        .interrupts = delivered_interrupts,
        .reload_count = last_reload,
        .mode = timer_mode,
        .calibrated_with_hpet = calibrated_with_hpet
    };
    scene_arch_interrupts_restore(state);
    return true;
}

void scene_local_timer_on_interrupt(void) {
    if (timer_mode != SCENE_LOCAL_TIMER_STOPPED) {
        ++delivered_interrupts;
        if (timer_mode == SCENE_LOCAL_TIMER_ONE_SHOT) {
            timer_mode = SCENE_LOCAL_TIMER_STOPPED;
        }
    }
}
