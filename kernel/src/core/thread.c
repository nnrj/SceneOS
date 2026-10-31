#include <scene/arch.h>
#include <scene/log.h>
#include <scene/paging.h>
#include <scene/physical.h>
#include <scene/thread.h>

#define SCENE_THREAD_SLOTS (SCENE_THREAD_CAPACITY + 1U)
#define SCENE_THREAD_STACK_REGION UINT64_C(0xffffc10000000000)
#define SCENE_THREAD_STACK_PAGES (SCENE_THREAD_STACK_SIZE / SCENE_PAGE_SIZE)
#define SCENE_THREAD_STACK_STRIDE (SCENE_THREAD_STACK_SIZE + 2U * SCENE_PAGE_SIZE)

struct thread {
    struct scene_arch_context continuation;
    struct scene_thread_info info;
    scene_thread_entry entry;
    void *context;
    uint64_t initial_interrupt_state;
    struct scene_wait_queue *wait_queue;
    size_t wait_next;
};

static struct thread threads[SCENE_THREAD_SLOTS];
static bool initialized;
static size_t current_slot;
static size_t thread_count;
static scene_thread_id next_id;

static uintptr_t stack_guard(size_t slot) {
    return (uintptr_t)(SCENE_THREAD_STACK_REGION +
        (slot - 1U) * SCENE_THREAD_STACK_STRIDE);
}

static size_t find_slot(scene_thread_id id) {
    if (id != 0) {
        for (size_t slot = 0; slot < SCENE_THREAD_SLOTS; ++slot) {
            if (threads[slot].info.id == id) {
                return slot;
            }
        }
    }
    return SCENE_THREAD_SLOTS;
}

static size_t next_ready(void) {
    for (size_t offset = 1; offset < SCENE_THREAD_SLOTS; ++offset) {
        const size_t slot = (current_slot + offset) % SCENE_THREAD_SLOTS;
        if (threads[slot].info.state == SCENE_THREAD_READY) {
            return slot;
        }
    }
    return current_slot;
}

/* IF stays clear across this call. It returns only when this continuation is
 * selected again; its caller owns IF restoration and any condition recheck. */
static void switch_to(size_t next, enum scene_thread_state previous_state) {
    struct thread *previous = &threads[current_slot];
    struct thread *selected = &threads[next];
    previous->info.state = previous_state;
    selected->info.state = SCENE_THREAD_RUNNING;
    current_slot = next;
    scene_arch_context_switch(&previous->continuation, &selected->continuation);
}

static void release_stack(uintptr_t bottom, size_t pages) {
    for (size_t index = 0; index < pages; ++index) {
        uint64_t physical;
        if (!scene_paging_unmap(bottom + index * SCENE_PAGE_SIZE, &physical) ||
            !scene_physical_free(physical)) {
            scene_panic("Thread stack release failed");
        }
    }
}

static _Noreturn void thread_start(void) {
    struct thread *thread = &threads[current_slot];
    if (current_slot == 0 || thread->info.state != SCENE_THREAD_RUNNING ||
        thread->entry == 0) {
        scene_panic("Invalid initial thread continuation");
    }
    scene_arch_interrupts_restore(thread->initial_interrupt_state);
    thread->entry(thread->context);
    scene_thread_exit();
}

bool scene_thread_init(void) {
    if (initialized || !scene_arch_interrupts_enabled() || !scene_paging_is_owned()) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    /* Reserve this range by convention until the VM layer has reservations. */
    for (size_t slot = 1; slot < SCENE_THREAD_SLOTS; ++slot) {
        for (size_t page = 0; page < SCENE_THREAD_STACK_PAGES + 2U; ++page) {
            uint64_t physical;
            if (scene_paging_translate(stack_guard(slot) + page * SCENE_PAGE_SIZE,
                    &physical)) {
                scene_arch_interrupts_restore(state);
                return false;
            }
        }
    }
    threads[0].info = (struct scene_thread_info){
        .id = 1, .state = SCENE_THREAD_RUNNING
    };
    current_slot = 0;
    next_id = 2;
    thread_count = 1;
    initialized = true;
    scene_arch_interrupts_restore(state);
    return true;
}

bool scene_thread_create(scene_thread_entry entry, void *context, scene_thread_id *id) {
    if (!initialized || entry == 0 || id == 0 || !scene_arch_interrupts_enabled()) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    size_t slot = 1;
    while (slot < SCENE_THREAD_SLOTS && threads[slot].info.id != 0) {
        ++slot;
    }
    if (slot == SCENE_THREAD_SLOTS || next_id == 0) {
        scene_arch_interrupts_restore(state);
        return false;
    }
    const uintptr_t bottom = stack_guard(slot) + SCENE_PAGE_SIZE;
    for (size_t page = 0; page < SCENE_THREAD_STACK_PAGES; ++page) {
        if (!scene_paging_map_new(bottom + page * SCENE_PAGE_SIZE, SCENE_PAGE_WRITABLE)) {
            release_stack(bottom, page);
            scene_arch_interrupts_restore(state);
            return false;
        }
    }
    struct scene_arch_context continuation;
    if (!scene_arch_context_prepare(&continuation, bottom, SCENE_THREAD_STACK_SIZE,
            thread_start)) {
        release_stack(bottom, SCENE_THREAD_STACK_PAGES);
        scene_arch_interrupts_restore(state);
        return false;
    }
    struct thread *created = &threads[slot];
    created->continuation = continuation;
    created->info = (struct scene_thread_info){
        .id = next_id,
        .state = SCENE_THREAD_READY,
        .stack_bottom = bottom,
        .stack_top = bottom + SCENE_THREAD_STACK_SIZE
    };
    created->entry = entry;
    created->context = context;
    created->initial_interrupt_state = state;
    created->wait_queue = 0;
    created->wait_next = 0;
    *id = next_id++;
    ++thread_count;
    scene_arch_interrupts_restore(state);
    return true;
}

bool scene_thread_yield(void) {
    if (!initialized || !scene_arch_interrupts_enabled()) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    const size_t next = next_ready();
    if (next != current_slot) {
        switch_to(next, SCENE_THREAD_READY);
    }
    scene_arch_interrupts_restore(state);
    return true;
}

_Noreturn void scene_thread_exit(void) {
    if (!initialized || current_slot == 0 || !scene_arch_interrupts_enabled()) {
        scene_panic("Invalid thread exit context");
    }
    (void)scene_arch_interrupts_save();
    const size_t next = next_ready();
    if (next == current_slot) {
        scene_panic("Thread exit has no runnable successor");
    }
    /* The exited stack stays mapped until another thread explicitly reaps it. */
    switch_to(next, SCENE_THREAD_EXITED);
    scene_panic("Exited thread resumed");
}

bool scene_thread_reap(scene_thread_id id) {
    if (!initialized || !scene_arch_interrupts_enabled()) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    const size_t slot = find_slot(id);
    if (slot == 0 || slot == SCENE_THREAD_SLOTS || slot == current_slot ||
        threads[slot].info.state != SCENE_THREAD_EXITED) {
        scene_arch_interrupts_restore(state);
        return false;
    }
    release_stack(threads[slot].info.stack_bottom, SCENE_THREAD_STACK_PAGES);
    /* Only the identity/state is live in a free slot; create overwrites the
     * remaining private continuation fields before publishing a fresh ID. */
    threads[slot].info = (struct scene_thread_info){0};
    --thread_count;
    scene_arch_interrupts_restore(state);
    return true;
}

bool scene_thread_get_info(scene_thread_id id, struct scene_thread_info *info) {
    if (!initialized || info == 0) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    const size_t slot = find_slot(id);
    if (slot != SCENE_THREAD_SLOTS) {
        *info = threads[slot].info;
    }
    scene_arch_interrupts_restore(state);
    return slot != SCENE_THREAD_SLOTS;
}

scene_thread_id scene_thread_current(void) {
    return initialized ? threads[current_slot].info.id : 0;
}

size_t scene_thread_count(void) {
    return thread_count;
}

static bool queue_valid(const struct scene_wait_queue *queue, size_t *count) {
    if (queue == 0) {
        return false;
    }
    size_t slot = queue->head;
    size_t last = 0;
    *count = 0;
    while (slot != 0) {
        if (slot >= SCENE_THREAD_SLOTS || *count >= SCENE_THREAD_CAPACITY ||
            threads[slot].info.state != SCENE_THREAD_BLOCKED ||
            threads[slot].wait_queue != queue) {
            return false;
        }
        ++*count;
        last = slot;
        slot = threads[slot].wait_next;
    }
    return queue->tail == last;
}

bool scene_thread_wait(
    struct scene_wait_queue *queue, scene_thread_condition condition, void *context
) {
    if (!initialized || current_slot == 0 || condition == 0 ||
        !scene_arch_interrupts_enabled()) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    size_t count;
    if (!queue_valid(queue, &count)) {
        scene_arch_interrupts_restore(state);
        return false;
    }
    while (!condition(context)) {
        const size_t next = next_ready();
        if (next == current_slot) {
            scene_panic("Thread wait has no runnable bootstrap context");
        }
        struct thread *thread = &threads[current_slot];
        thread->wait_queue = queue;
        thread->wait_next = 0;
        if (queue->tail != 0) {
            threads[queue->tail].wait_next = current_slot;
        } else {
            queue->head = current_slot;
        }
        queue->tail = current_slot;
        switch_to(next, SCENE_THREAD_BLOCKED);
        /* A wake removes our node before marking READY. Do not treat the wake
         * itself as evidence that the caller's condition became true. */
    }
    scene_arch_interrupts_restore(state);
    return true;
}

static void wake_head(struct scene_wait_queue *queue) {
    struct thread *thread = &threads[queue->head];
    queue->head = thread->wait_next;
    if (queue->head == 0) {
        queue->tail = 0;
    }
    thread->wait_next = 0;
    thread->wait_queue = 0;
    thread->info.state = SCENE_THREAD_READY;
}

bool scene_wait_queue_wake_one(struct scene_wait_queue *queue) {
    if (!initialized) {
        return false;
    }
    const uint64_t state = scene_arch_interrupts_save();
    size_t count;
    const bool wake = queue_valid(queue, &count) && count != 0;
    if (wake) {
        wake_head(queue);
    }
    scene_arch_interrupts_restore(state);
    return wake;
}

size_t scene_wait_queue_wake_all(struct scene_wait_queue *queue) {
    if (!initialized) {
        return 0;
    }
    const uint64_t state = scene_arch_interrupts_save();
    size_t count;
    if (!queue_valid(queue, &count)) {
        scene_arch_interrupts_restore(state);
        return 0;
    }
    for (size_t index = 0; index < count; ++index) {
        wake_head(queue);
    }
    scene_arch_interrupts_restore(state);
    return count;
}

size_t scene_wait_queue_pending(const struct scene_wait_queue *queue) {
    if (!initialized) {
        return 0;
    }
    const uint64_t state = scene_arch_interrupts_save();
    size_t count;
    const bool valid = queue_valid(queue, &count);
    scene_arch_interrupts_restore(state);
    return valid ? count : 0;
}
