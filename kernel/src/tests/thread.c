#include "thread.h"

#include <scene/arch.h>
#include <scene/clock.h>
#include <scene/log.h>
#include <scene/paging.h>
#include <scene/physical.h>
#include <scene/thread.h>
#include <scene/timer.h>

extern bool scene_test_thread_registers(uint64_t seed);

struct worker_probe {
    scene_thread_id id;
    size_t index;
    bool complete;
    bool failed;
};

static size_t trace[SCENE_THREAD_CAPACITY * 4U];
static size_t trace_count;

static void empty_worker(void *context) {
    (void)context;
}

static void worker(void *context) {
    struct worker_probe *probe = context;
    volatile uint64_t canaries[64];
    for (size_t index = 0; index < 64U; ++index) {
        canaries[index] = probe->id * 1000U + index;
    }
    struct scene_thread_info info;
    if (scene_thread_current() != probe->id ||
        !scene_thread_get_info(probe->id, &info) || info.state != SCENE_THREAD_RUNNING ||
        (uintptr_t)&canaries[0] < info.stack_bottom ||
        (uintptr_t)(&canaries[63] + 1) > info.stack_top ||
        scene_thread_reap(probe->id) || !scene_arch_interrupts_enabled()) {
        probe->failed = true;
        return;
    }
    for (size_t iteration = 0; iteration < 4U; ++iteration) {
        if (trace_count >= sizeof(trace) / sizeof(trace[0])) {
            probe->failed = true;
            return;
        }
        trace[trace_count++] = probe->index;
        if (!scene_thread_yield()) {
            probe->failed = true;
            return;
        }
    }
    if (!scene_test_thread_registers(probe->id)) {
        probe->failed = true;
        return;
    }
    const uint64_t interrupt_state = scene_arch_interrupts_save();
    scene_thread_id untouched = UINT64_MAX;
    const bool rejected = !scene_thread_yield() &&
        !scene_thread_create(empty_worker, 0, &untouched) &&
        untouched == UINT64_MAX && !scene_thread_reap(probe->id) &&
        !scene_arch_interrupts_enabled();
    scene_arch_interrupts_restore(interrupt_state);
    if (!rejected) {
        probe->failed = true;
        return;
    }
    /* Timer IRQs must continue while workers execute and yield. */
    const uint64_t start_ticks = scene_timer_ticks();
    const uint64_t start = scene_clock_monotonic_ns();
    while (scene_timer_ticks() - start_ticks < 2U) {
        if (scene_clock_monotonic_ns() - start > UINT64_C(500000000) ||
            !scene_thread_yield()) {
            probe->failed = true;
            return;
        }
    }
    for (size_t index = 0; index < 64U; ++index) {
        if (canaries[index] != probe->id * 1000U + index) {
            probe->failed = true;
            return;
        }
    }
    probe->complete = true;
    if (probe->index == SCENE_THREAD_CAPACITY - 1U) {
        scene_thread_exit();
    }
    /* All other workers return through the automatic exit trampoline. */
}

static bool verify_stack(const struct scene_thread_info *info, bool mapped) {
    uint64_t physical;
    if (info->stack_top - info->stack_bottom != SCENE_THREAD_STACK_SIZE ||
        scene_paging_translate(info->stack_bottom - SCENE_PAGE_SIZE, &physical) ||
        scene_paging_translate(info->stack_top, &physical)) {
        return false;
    }
    for (size_t offset = 0; offset < SCENE_THREAD_STACK_SIZE; offset += SCENE_PAGE_SIZE) {
        if (scene_paging_translate(info->stack_bottom + offset, &physical) != mapped) {
            return false;
        }
    }
    return true;
}

static bool verify_creation_rollback(uintptr_t bottom) {
    /* Occupy the fourth page, forcing creation to unwind three mapped pages. */
    const uintptr_t collision = bottom + 3U * SCENE_PAGE_SIZE;
    if (!scene_paging_map_new(collision, SCENE_PAGE_WRITABLE)) {
        return false;
    }
    const uint64_t before = scene_physical_free_pages();
    scene_thread_id id = UINT64_MAX;
    const bool rejected = !scene_thread_create(empty_worker, 0, &id) &&
        id == UINT64_MAX && scene_thread_count() == 1U &&
        scene_physical_free_pages() == before;
    uint64_t physical;
    const bool released = scene_paging_unmap(collision, &physical) &&
        scene_physical_free(physical);
    struct scene_thread_info info = {
        .stack_bottom = bottom, .stack_top = bottom + SCENE_THREAD_STACK_SIZE
    };
    return rejected && released && verify_stack(&info, false);
}

bool scene_thread_self_test(void) {
    const scene_thread_id bootstrap = scene_thread_current();
    scene_thread_id untouched = UINT64_MAX;
    struct scene_thread_info info;
    if (bootstrap == 0 || scene_thread_count() != 1U || scene_thread_init() ||
        !scene_thread_get_info(bootstrap, &info) || info.state != SCENE_THREAD_RUNNING ||
        info.stack_bottom != 0 || info.stack_top != 0 ||
        scene_thread_create(0, 0, &untouched) || untouched != UINT64_MAX ||
        scene_thread_create(empty_worker, 0, 0) || scene_thread_reap(bootstrap) ||
        scene_thread_reap(0) || scene_thread_get_info(bootstrap, 0) ||
        !scene_thread_yield() || scene_thread_current() != bootstrap) {
        return false;
    }
    const struct scene_thread_info sentinel = {UINT64_MAX, SCENE_THREAD_EXITED, 1, 2};
    info = sentinel;
    if (scene_thread_get_info(0, &info) || info.id != sentinel.id ||
        info.state != sentinel.state || info.stack_bottom != 1 || info.stack_top != 2) {
        return false;
    }
    scene_log_line("Thread API rejection and bootstrap yield: verified");
    uint64_t warm_free_pages = 0;
    scene_thread_id retired = 0;
    uintptr_t first_bottom = 0;
    for (size_t round = 0; round < 3U; ++round) {
        scene_log_u64("Thread self-test round: ", round);
        struct worker_probe probes[SCENE_THREAD_CAPACITY];
        struct scene_thread_info snapshots[SCENE_THREAD_CAPACITY];
        trace_count = 0;
        for (size_t index = 0; index < SCENE_THREAD_CAPACITY; ++index) {
            probes[index].id = 0;
            probes[index].index = index;
            probes[index].complete = false;
            probes[index].failed = false;
            if (!scene_thread_create(worker, &probes[index], &probes[index].id) ||
                !scene_thread_get_info(probes[index].id, &snapshots[index]) ||
                snapshots[index].state != SCENE_THREAD_READY ||
                !verify_stack(&snapshots[index], true) ||
                *(const uint64_t *)(snapshots[index].stack_bottom + 64U) != 0 ||
                scene_thread_reap(probes[index].id)) {
                return false;
            }
        }
        if (round == 0) {
            first_bottom = snapshots[0].stack_bottom;
        } else if (snapshots[0].stack_bottom != first_bottom ||
            probes[0].id <= retired || scene_thread_get_info(retired, &info) ||
            scene_thread_reap(retired)) {
            return false;
        }
        untouched = UINT64_MAX;
        if (scene_thread_count() != SCENE_THREAD_CAPACITY + 1U ||
            scene_thread_create(empty_worker, 0, &untouched) || untouched != UINT64_MAX) {
            return false;
        }
        const uint64_t start = scene_clock_monotonic_ns();
        size_t exited = 0;
        while (exited != SCENE_THREAD_CAPACITY) {
            if (scene_clock_monotonic_ns() - start > UINT64_C(3000000000) ||
                !scene_thread_yield()) {
                return false;
            }
            exited = 0;
            for (size_t index = 0; index < SCENE_THREAD_CAPACITY; ++index) {
                if (probes[index].failed ||
                    !scene_thread_get_info(probes[index].id, &info)) {
                    scene_log_u64("Failed thread worker index: ", index);
                    return false;
                }
                if (info.state == SCENE_THREAD_EXITED) {
                    if (!probes[index].complete) {
                        return false;
                    }
                    ++exited;
                }
            }
        }
        if (trace_count != SCENE_THREAD_CAPACITY * 4U) {
            return false;
        }
        for (size_t index = 0; index < trace_count; ++index) {
            if (trace[index] != index % SCENE_THREAD_CAPACITY) {
                return false;
            }
        }
        retired = probes[0].id;
        /* Exited threads retain slots and mapped stacks until explicit reap. */
        if (scene_thread_create(empty_worker, 0, &untouched) ||
            scene_thread_count() != SCENE_THREAD_CAPACITY + 1U) {
            return false;
        }
        for (size_t index = 0; index < SCENE_THREAD_CAPACITY; ++index) {
            if (!verify_stack(&snapshots[index], true) ||
                !scene_thread_reap(probes[index].id) ||
                scene_thread_reap(probes[index].id) ||
                scene_thread_get_info(probes[index].id, &info) ||
                !verify_stack(&snapshots[index], false)) {
                return false;
            }
        }
        if (scene_thread_current() != bootstrap || scene_thread_count() != 1U ||
            !scene_arch_interrupts_enabled()) {
            return false;
        }
        if (round == 0) {
            /* The paging layer retains intermediate tables after first use. */
            warm_free_pages = scene_physical_free_pages();
            if (!verify_creation_rollback(first_bottom)) {
                return false;
            }
            scene_log_line("Thread partial stack allocation rollback: verified");
        }
        if (scene_physical_free_pages() != warm_free_pages) {
            return false;
        }
    }
    scene_log_line("Thread round-robin execution order: verified");
    scene_log_line("Thread callee-saved registers and stack alignment: verified");
    scene_log_line("Thread stack isolation and guard mappings: verified");
    scene_log_line("Thread capacity, exit, stale IDs and page reclamation: verified");
    scene_log_line("Thread IRQ exclusion and PIT progress: verified");
    scene_log_u64("Thread workers completed: ", 3U * SCENE_THREAD_CAPACITY);
    scene_log_u64("Thread live records: ", scene_thread_count());
    return true;
}

static void faulting_worker(void *context) {
    (void)context;
    struct scene_thread_info info;
    if (!scene_thread_get_info(scene_thread_current(), &info)) {
        scene_panic("Missing thread guard-test context");
    }
    const uintptr_t guard = info.stack_bottom - SCENE_PAGE_SIZE;
    scene_log_hex("Thread guard page expected address: ", guard);
    scene_log_line("Triggering thread stack guard self-test");
    *(volatile uint64_t *)guard = UINT64_C(0xdeadbeef);
    scene_panic("Thread guard page accepted a write");
}

_Noreturn void scene_thread_guard_self_test(void) {
    scene_thread_id id;
    if (!scene_thread_create(faulting_worker, 0, &id) || !scene_thread_yield()) {
        scene_panic("Unable to start thread guard self-test");
    }
    scene_panic("Thread guard self-test unexpectedly returned");
}
