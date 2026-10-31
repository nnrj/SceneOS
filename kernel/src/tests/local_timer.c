#include "local_timer.h"
#include "../core/local_timer_math.h"

#include <scene/arch.h>
#include <scene/clock.h>
#include <scene/irq.h>
#include <scene/local_timer.h>
#include <scene/log.h>
#include <scene/timer.h>

#include <stddef.h>

static bool verify_math(void) {
    uint64_t frequency = 123U;
    if (!scene_local_timer_measure_frequency(
            1000000U, 500000U, 8000000U, 100000000U, &frequency
        ) || frequency != 6250000U ||
        !scene_local_timer_measure_frequency(
            1000000U, 500000U, 8U, 100U, &frequency
        ) || frequency != 6250000U) {
        return false;
    }
    static const struct {
        uint32_t initial;
        uint32_t final;
        uint64_t ticks;
        uint64_t hz;
    } invalid_measurements[] = {
        {10U, 0U, 8U, 100U},
        {10U, 10U, 8U, 100U},
        {10U, 11U, 8U, 100U},
        {10U, 1U, 0U, 100U},
        {10U, 1U, 8U, 0U},
        {10U, 1U, 8U, UINT64_MAX},
        {2U, 1U, 1U, UINT64_C(4294967296)},
        {2U, 1U, 100U, 1U}
    };
    for (size_t index = 0; index < sizeof(invalid_measurements) /
        sizeof(invalid_measurements[0]); ++index) {
        frequency = 123U;
        if (scene_local_timer_measure_frequency(
                invalid_measurements[index].initial,
                invalid_measurements[index].final,
                invalid_measurements[index].ticks,
                invalid_measurements[index].hz, &frequency
            ) || frequency != 123U) {
            return false;
        }
    }
    static const struct {
        uint64_t hz;
        uint64_t ns;
        uint32_t expected;
        bool valid;
    } one_shots[] = {
        {62500000U, 10000000U, 625000U, true},
        {3U, 1U, 1U, true},
        {3U, 333333333U, 1U, true},
        {3U, 333333334U, 2U, true},
        {UINT32_MAX, 1000000000U, UINT32_MAX, true},
        {UINT32_MAX, 1000000001U, 0, false},
        {1U, UINT64_C(4294967295000000000), UINT32_MAX, true},
        {1U, UINT64_C(4294967295000000001), 0, false},
        {62500000U, UINT64_MAX, 0, false},
        {0, 10000000U, 0, false},
        {UINT64_MAX, 1U, 0, false},
        {62500000U, 0, 0, false}
    };
    for (size_t index = 0;
        index < sizeof(one_shots) / sizeof(one_shots[0]); ++index) {
        uint32_t count = 0;
        const bool valid = scene_local_timer_one_shot_count(
            one_shots[index].hz, one_shots[index].ns, &count
        );
        if (valid != one_shots[index].valid ||
            count != one_shots[index].expected) {
            return false;
        }
    }
    uint32_t count = 0;
    return scene_local_timer_periodic_count(62500000U, 100U, &count) &&
        count == 625000U &&
        scene_local_timer_periodic_count(1001U, 100U, &count) && count == 10U &&
        scene_local_timer_periodic_count(UINT32_MAX, 1U, &count) &&
        count == UINT32_MAX &&
        !scene_local_timer_periodic_count(100U, 101U, &count) &&
        !scene_local_timer_periodic_count(100U, 0, &count) &&
        !scene_local_timer_periodic_count(0, 1U, &count) &&
        !scene_local_timer_periodic_count(UINT64_MAX, 1U, &count) &&
        !scene_local_timer_periodic_count(100U, 1U, 0) &&
        !scene_local_timer_one_shot_count(100U, 1U, 0) &&
        !scene_local_timer_measure_frequency(10U, 1U, 8U, 100U, 0);
}

static bool verify_pit_cadence(void) {
    if (!scene_arch_hpet_ready()) {
        return true;
    }
    if (!scene_timer_wait_ticks(1U)) {
        return false;
    }
    const uint64_t start = scene_arch_hpet_counter();
    const uint64_t pit_start = scene_timer_ticks();
    if (!scene_timer_wait_ticks(8U)) {
        return false;
    }
    const uint64_t pit_ticks = scene_timer_ticks() - pit_start;
    const uint64_t elapsed = scene_arch_hpet_counter() - start;
    const uint64_t hpet_hz = scene_arch_hpet_frequency_hz();
    if (pit_ticks == 0 || hpet_hz == 0 || elapsed == 0 ||
        pit_ticks > UINT64_MAX / hpet_hz) {
        return false;
    }
    const uint64_t measured_hz = pit_ticks * hpet_hz / elapsed;
    const uint64_t expected_hz = scene_timer_frequency();
    scene_log_u64("PIT measured frequency Hz: ", measured_hz);
    /* Broad emulator tolerance, but reject a doubled/missing tick stream. */
    if (measured_hz < expected_hz * 3U / 4U ||
        measured_hz > expected_hz * 5U / 4U) {
        return false;
    }
    scene_log_line("PIT cadence against HPET: verified");
    return true;
}

static bool wait_for_interrupts(uint64_t target) {
    const uint64_t start = scene_clock_monotonic_ns();
    const uint64_t watchdog = scene_arch_timestamp_counter();
    struct scene_local_timer_info info;
    do {
        if (!scene_local_timer_get_info(&info)) {
            return false;
        }
        if (info.interrupts >= target) {
            return true;
        }
    } while (scene_clock_monotonic_ns() - start < UINT64_C(500000000) &&
        scene_arch_timestamp_counter() - watchdog < UINT64_C(2000000000));
    return false;
}

static bool verify_pending_interrupt(uint64_t delivered_before) {
    const uint64_t state = scene_arch_interrupts_save();
    bool verified = scene_local_timer_start_one_shot(UINT64_C(1000000));
    const uint64_t start = scene_clock_monotonic_ns();
    const uint64_t watchdog = scene_arch_timestamp_counter();
    while (verified && !scene_arch_local_timer_pending()) {
        if (scene_clock_monotonic_ns() - start >= UINT64_C(500000000) ||
            scene_arch_timestamp_counter() - watchdog >=
                UINT64_C(2000000000)) {
            verified = false;
        }
    }
    scene_local_timer_stop();
    verified = verified &&
        !scene_local_timer_start_one_shot(UINT64_C(5000000)) &&
        !scene_arch_interrupts_enabled();
    scene_arch_interrupts_restore(state);

    struct scene_local_timer_info info;
    if (!verified || !scene_timer_wait_ticks(3U) ||
        scene_arch_local_timer_pending() ||
        !scene_local_timer_get_info(&info) ||
        info.interrupts != delivered_before ||
        !scene_arch_interrupts_enabled()) {
        return false;
    }
    return scene_local_timer_start_one_shot(UINT64_C(5000000)) &&
        wait_for_interrupts(delivered_before + 1U) &&
        scene_local_timer_get_info(&info) &&
        info.interrupts == delivered_before + 1U &&
        info.mode == SCENE_LOCAL_TIMER_STOPPED;
}

bool scene_local_timer_self_test(void) {
    struct scene_local_timer_info info;
    if (!verify_math() || scene_local_timer_ready() ||
        scene_local_timer_get_info(&info) ||
        scene_local_timer_start_periodic(100U) ||
        scene_local_timer_start_one_shot(UINT64_C(1000000))) {
        return false;
    }
    scene_log_line("Local timer arithmetic boundaries: verified");
    if (!verify_pit_cadence()) {
        return false;
    }
    const size_t registered_irqs = scene_irq_registered_count();
    const uint64_t pit_before = scene_timer_ticks();
    if (!scene_arch_local_timer_available()) {
        if (scene_local_timer_init(8U) || !scene_timer_wait_ticks(3U)) {
            return false;
        }
        scene_log_line("Local timer unavailable: PIT fallback verified");
        return scene_irq_registered_count() == registered_irqs &&
            scene_timer_ticks() > pit_before;
    }
    const uint64_t state = scene_arch_interrupts_save();
    const bool rejected_disabled = !scene_local_timer_init(8U);
    scene_arch_interrupts_restore(state);
    if (!rejected_disabled || scene_local_timer_init(0) ||
        scene_local_timer_init(17U) || !scene_local_timer_init(8U) ||
        scene_local_timer_init(8U) ||
        !scene_local_timer_get_info(&info) || scene_local_timer_get_info(0) ||
        info.counter_frequency_hz == 0 || info.interrupts != 0 ||
        info.mode != SCENE_LOCAL_TIMER_STOPPED) {
        return false;
    }
    scene_log_line(info.calibrated_with_hpet
        ? "Local timer calibration reference: HPET"
        : "Local timer calibration reference: PIT");
    scene_log_u64("Local timer counter frequency Hz: ",
        info.counter_frequency_hz);
    if (scene_local_timer_start_one_shot(0) ||
        scene_local_timer_start_one_shot(UINT64_MAX) ||
        scene_local_timer_start_periodic(0) ||
        !scene_local_timer_start_periodic(100U) ||
        scene_local_timer_start_periodic(100U) ||
        scene_local_timer_start_one_shot(UINT64_C(1000000)) ||
        !wait_for_interrupts(4U)) {
        return false;
    }
    scene_local_timer_stop();
    if (!scene_local_timer_get_info(&info)) {
        return false;
    }
    const uint64_t periodic_interrupts = info.interrupts;
    scene_log_u64("Local timer periodic interrupts: ", periodic_interrupts);
    if (!scene_timer_wait_ticks(3U) || !scene_local_timer_get_info(&info) ||
        info.interrupts != periodic_interrupts ||
        info.mode != SCENE_LOCAL_TIMER_STOPPED ||
        scene_arch_local_timer_current_count() != 0 ||
        !scene_arch_interrupts_enabled()) {
        return false;
    }
    scene_log_line("Local timer periodic delivery/stop: verified");
    if (!scene_local_timer_start_one_shot(UINT64_C(10000000)) ||
        !wait_for_interrupts(periodic_interrupts + 1U) ||
        !scene_timer_wait_ticks(3U) || !scene_local_timer_get_info(&info) ||
        info.interrupts != periodic_interrupts + 1U ||
        info.mode != SCENE_LOCAL_TIMER_STOPPED) {
        return false;
    }
    scene_log_line("Local timer one-shot delivery: verified");
    if (!verify_pending_interrupt(info.interrupts) ||
        scene_irq_registered_count() != registered_irqs ||
        scene_timer_ticks() <= pit_before) {
        return false;
    }
    scene_log_line("Local timer pending interrupt drain/rearm: verified");
    scene_log_line("PIT compatibility ticks: verified");
    return true;
}
