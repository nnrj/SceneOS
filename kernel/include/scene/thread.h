#ifndef SCENE_THREAD_H
#define SCENE_THREAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Worker slots; the bootstrap thread occupies one additional permanent slot. */
#define SCENE_THREAD_CAPACITY 8U
#define SCENE_THREAD_STACK_SIZE (32U * 1024U)

typedef uint64_t scene_thread_id;
typedef void (*scene_thread_entry)(void *context);
/* Called with IF clear; must not block, yield, or enable interrupts. */
typedef bool (*scene_thread_condition)(void *context);

/* Initialize to {0}; fields are private slot links, never copy a live queue.
 * Keep the queue and condition context alive until all waiters finish. */
struct scene_wait_queue {
    size_t head;
    size_t tail;
};

enum scene_thread_state {
    SCENE_THREAD_READY = 1,
    SCENE_THREAD_RUNNING,
    SCENE_THREAD_EXITED,
    SCENE_THREAD_BLOCKED
};

struct scene_thread_info {
    scene_thread_id id;
    enum scene_thread_state state;
    uintptr_t stack_bottom;
    uintptr_t stack_top; /* Exclusive; both bounds are zero for bootstrap. */
};

/* BSP-only cooperative threads sharing the kernel address space. Init, create,
 * yield, wait, exit and reap require ordinary context with IF set. No FPU/SIMD,
 * blocking join, preemption, or concurrent input consumers are provided. */
bool scene_thread_init(void);
bool scene_thread_create(scene_thread_entry entry, void *context, scene_thread_id *id);
bool scene_thread_yield(void);
_Noreturn void scene_thread_exit(void);
bool scene_thread_reap(scene_thread_id id);
bool scene_thread_get_info(scene_thread_id id, struct scene_thread_info *info);
scene_thread_id scene_thread_current(void);
size_t scene_thread_count(void); /* Includes bootstrap and unreaped exits. */

/* Worker-only wait. Check the condition and enqueue atomically against IRQs;
 * recheck after every wake. Bootstrap must stay runnable and drive yield. */
bool scene_thread_wait(
    struct scene_wait_queue *queue, scene_thread_condition condition, void *context
);
/* Wakers may run in maskable IRQ context; they mark READY without switching. */
bool scene_wait_queue_wake_one(struct scene_wait_queue *queue);
size_t scene_wait_queue_wake_all(struct scene_wait_queue *queue);
size_t scene_wait_queue_pending(const struct scene_wait_queue *queue);

#endif
