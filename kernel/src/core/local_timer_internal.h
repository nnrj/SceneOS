#ifndef SCENE_CORE_LOCAL_TIMER_INTERNAL_H
#define SCENE_CORE_LOCAL_TIMER_INTERNAL_H

/* Architecture interrupt entry only; EOI remains the dispatcher's job. */
void scene_local_timer_on_interrupt(void);

#endif
