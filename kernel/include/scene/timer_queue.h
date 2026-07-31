#ifndef SCENE_TIMER_QUEUE_H
#define SCENE_TIMER_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_TIMER_QUEUE_CAPACITY 32U

typedef uint64_t scene_timer_id;
typedef void (*scene_timer_callback)(void *context);

void scene_timer_queue_init(void);
bool scene_timer_schedule_at(
    uint64_t deadline_ns,
    scene_timer_callback callback,
    void *context,
    scene_timer_id *timer_id
);
bool scene_timer_schedule_after(
    uint64_t delay_ns,
    scene_timer_callback callback,
    void *context,
    scene_timer_id *timer_id
);
bool scene_timer_cancel(scene_timer_id timer_id);
size_t scene_timer_run_due(void);
size_t scene_timer_pending(void);

#endif
