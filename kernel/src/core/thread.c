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
    threads[slot] = (struct thread){
        .continuation = continuation,
        .info = {
            .id = next_id,
            .state = SCENE_THREAD_READY,
            .stack_bottom = bottom,
            .stack_top = bottom + SCENE_THREAD_STACK_SIZE
        },
        .entry = entry,
        .context = context,
        .initial_interrupt_state = state
    };
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
        struct thread *previous = &threads[current_slot];
        struct thread *selected = &threads[next];
        previous->info.state = SCENE_THREAD_READY;
        selected->info.state = SCENE_THREAD_RUNNING;
        current_slot = next;
        scene_arch_context_switch(&previous->continuation, &selected->continuation);
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
    struct thread *previous = &threads[current_slot];
    struct thread *selected = &threads[next];
    previous->info.state = SCENE_THREAD_EXITED;
    selected->info.state = SCENE_THREAD_RUNNING;
    current_slot = next;
    /* The exited stack stays mapped until another thread explicitly reaps it. */
    scene_arch_context_switch(&previous->continuation, &selected->continuation);
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
    threads[slot] = (struct thread){0};
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
