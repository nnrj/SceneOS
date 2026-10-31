#ifndef SCENE_LOCAL_TIMER_H
#define SCENE_LOCAL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

enum scene_local_timer_mode {
    SCENE_LOCAL_TIMER_STOPPED = 0,
    SCENE_LOCAL_TIMER_ONE_SHOT,
    SCENE_LOCAL_TIMER_PERIODIC
};

struct scene_local_timer_info {
    uint64_t counter_frequency_hz;
    uint64_t interrupts;
    uint32_t reload_count;
    enum scene_local_timer_mode mode;
    bool calibrated_with_hpet;
};

bool scene_local_timer_init(uint64_t calibration_ticks);
bool scene_local_timer_ready(void);
bool scene_local_timer_start_one_shot(uint64_t delay_ns);
bool scene_local_timer_start_periodic(uint32_t frequency_hz);
void scene_local_timer_stop(void);
bool scene_local_timer_get_info(struct scene_local_timer_info *info);

#endif
