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

enum scene_thread_state {
    SCENE_THREAD_READY = 1,
    SCENE_THREAD_RUNNING,
    SCENE_THREAD_EXITED
};

struct scene_thread_info {
    scene_thread_id id;
    enum scene_thread_state state;
    uintptr_t stack_bottom;
    uintptr_t stack_top; /* Exclusive; both bounds are zero for bootstrap. */
};

/* BSP-only cooperative threads sharing the kernel address space. Mutating
 * calls require ordinary thread context with interrupts enabled. No FPU/SIMD,
 * blocking join, preemption, or concurrent input consumers are provided. */
bool scene_thread_init(void);
bool scene_thread_create(scene_thread_entry entry, void *context, scene_thread_id *id);
bool scene_thread_yield(void);
_Noreturn void scene_thread_exit(void);
bool scene_thread_reap(scene_thread_id id);
bool scene_thread_get_info(scene_thread_id id, struct scene_thread_info *info);
scene_thread_id scene_thread_current(void);
size_t scene_thread_count(void); /* Includes bootstrap and unreaped exits. */

#endif
