#include "thread_wait.h"

#include <scene/arch.h>
#include <scene/clock.h>
#include <scene/input.h>
#include <scene/log.h>
#include <scene/thread.h>

struct wait_probe {
    struct scene_wait_queue *queue;
    scene_thread_id id;
    size_t checks;
    bool ready;
    bool complete;
    bool failed;
};

static bool ready(void *context) {
    struct wait_probe *probe = context;
    ++probe->checks;
    if (scene_arch_interrupts_enabled()) {
        probe->failed = true;
    }
    return probe->ready;
}

static void waiting_worker(void *context) {
    struct wait_probe *probe = context;
    struct scene_wait_queue invalid = {SCENE_THREAD_CAPACITY + 1U, 0};
    if (scene_thread_wait(0, ready, probe) ||
        scene_thread_wait(probe->queue, 0, probe) ||
        scene_thread_wait(&invalid, ready, probe) || probe->checks != 0) {
        probe->failed = true;
        return;
    }
    const uint64_t state = scene_arch_interrupts_save();
    const bool rejected = !scene_thread_wait(probe->queue, ready, probe) &&
        !scene_arch_interrupts_enabled() && probe->checks == 0;
    scene_arch_interrupts_restore(state);
    if (!rejected || !scene_thread_wait(probe->queue, ready, probe) ||
        !scene_arch_interrupts_enabled()) {
        probe->failed = true;
        return;
    }
    probe->complete = true;
}

static bool has_state(scene_thread_id id, enum scene_thread_state state) {
    struct scene_thread_info info;
    return scene_thread_get_info(id, &info) && info.state == state;
}

static bool verify_wait_queue(void) {
    struct scene_wait_queue queue = {0};
    struct scene_wait_queue invalid = {SCENE_THREAD_CAPACITY + 1U, 0};
    struct wait_probe probes[3];
    if (scene_wait_queue_wake_one(0) || scene_wait_queue_wake_all(0) != 0 ||
        scene_wait_queue_pending(0) != 0 || scene_wait_queue_wake_one(&invalid) ||
        scene_wait_queue_wake_all(&invalid) != 0 || scene_wait_queue_pending(&invalid) != 0) {
        return false;
    }
    for (size_t index = 0; index < 3U; ++index) {
        probes[index].queue = &queue;
        probes[index].id = 0;
        probes[index].checks = 0;
        probes[index].ready = false;
        probes[index].complete = false;
        probes[index].failed = false;
        if (!scene_thread_create(waiting_worker, &probes[index], &probes[index].id)) {
            return false;
        }
    }
    if (scene_thread_wait(&queue, ready, &probes[0]) || probes[0].checks != 0 ||
        !scene_thread_yield() || scene_wait_queue_pending(&queue) != 3U) {
        return false;
    }
    for (size_t index = 0; index < 3U; ++index) {
        if (probes[index].failed || probes[index].checks != 1U ||
            !has_state(probes[index].id, SCENE_THREAD_BLOCKED) ||
            scene_thread_reap(probes[index].id)) {
            return false;
        }
    }
    /* Simulate the IRQ critical section: wake preserves IF and cannot run the
     * waiter inline. A false condition must send it to the queue's tail. */
    const uint64_t state = scene_arch_interrupts_save();
    const bool woken = scene_wait_queue_wake_one(&queue) &&
        !scene_arch_interrupts_enabled() && !probes[0].complete &&
        has_state(probes[0].id, SCENE_THREAD_READY);
    scene_arch_interrupts_restore(state);
    if (!woken || !scene_thread_yield() || probes[0].checks != 2U ||
        probes[0].complete || scene_wait_queue_pending(&queue) != 3U ||
        !has_state(probes[0].id, SCENE_THREAD_BLOCKED)) {
        return false;
    }
    probes[1].ready = true;
    if (!scene_wait_queue_wake_one(&queue) ||
        !has_state(probes[1].id, SCENE_THREAD_READY) ||
        !has_state(probes[0].id, SCENE_THREAD_BLOCKED) ||
        !scene_thread_yield() || !probes[1].complete ||
        !has_state(probes[1].id, SCENE_THREAD_EXITED) ||
        scene_wait_queue_pending(&queue) != 2U) {
        return false;
    }
    probes[0].ready = true;
    probes[2].ready = true;
    if (scene_wait_queue_wake_all(&queue) != 2U || scene_wait_queue_pending(&queue) != 0 ||
        !scene_thread_yield()) {
        return false;
    }
    for (size_t index = 0; index < 3U; ++index) {
        if (!probes[index].complete || probes[index].failed ||
            !has_state(probes[index].id, SCENE_THREAD_EXITED) ||
            !scene_thread_reap(probes[index].id)) {
            return false;
        }
    }
    /* A condition set before the worker registers cannot lose a wakeup. */
    probes[0].checks = 0;
    probes[0].complete = false;
    if (scene_wait_queue_wake_one(&queue) ||
        !scene_thread_create(waiting_worker, &probes[0], &probes[0].id) ||
        !scene_thread_yield() || probes[0].checks != 1U || !probes[0].complete ||
        probes[0].failed || scene_wait_queue_pending(&queue) != 0 ||
        !scene_thread_reap(probes[0].id) || scene_thread_count() != 1U) {
        return false;
    }
    scene_log_line("Wait queue FIFO and predicate recheck: verified");
    scene_log_line("Wait queue atomic condition and IRQ-safe wake: verified");
    return true;
}

struct input_probe {
    size_t count;
    bool failed;
};

static void input_worker(void *context) {
    struct input_probe *probe = context;
    static const struct scene_input_event expected[] = {
        {.type = SCENE_INPUT_EVENT_POINTER, .pointer = {2, -1, 0, 0, 0}},
        {.type = SCENE_INPUT_EVENT_KEY, .key = {SCENE_KEY_A, 0, true, false}},
        {.type = SCENE_INPUT_EVENT_KEY, .key = {SCENE_KEY_A, 0, false, false}},
        {.type = SCENE_INPUT_EVENT_POINTER, .pointer = {12, -7, 0, 0, 0}},
        {.type = SCENE_INPUT_EVENT_POINTER, .pointer = {0, 0, 1, 0, 0}},
        {.type = SCENE_INPUT_EVENT_POINTER, .pointer = {0, 0, 0, 1, 1}},
        {.type = SCENE_INPUT_EVENT_POINTER, .pointer = {0, 0, 0, 0, 1}}
    };
    uint64_t last_sequence = 0;
    for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        struct scene_input_event event;
        if (!scene_input_wait(&event) || event.type != expected[index].type ||
            event.sequence <= last_sequence || !scene_arch_interrupts_enabled()) {
            probe->failed = true;
            return;
        }
        last_sequence = event.sequence;
        if (event.type == SCENE_INPUT_EVENT_KEY) {
            const struct scene_key_event *key = &expected[index].key;
            if (event.key.code != key->code || event.key.pressed != key->pressed ||
                event.key.modifiers != key->modifiers || event.key.repeat) {
                probe->failed = true;
                return;
            }
        } else {
            const struct scene_pointer_event *pointer = &expected[index].pointer;
            if (event.pointer.delta_x != pointer->delta_x ||
                event.pointer.delta_y != pointer->delta_y ||
                event.pointer.wheel_delta != pointer->wheel_delta ||
                event.pointer.buttons != pointer->buttons ||
                event.pointer.changed_buttons != pointer->changed_buttons) {
                probe->failed = true;
                return;
            }
        }
        ++probe->count;
    }
}

static bool verify_input_wait(void) {
    struct input_probe probe = {0};
    scene_thread_id id;
    struct scene_input_event untouched = {.sequence = UINT64_MAX};
    if (scene_input_wait(&untouched) || untouched.sequence != UINT64_MAX ||
        scene_input_wait(0) || scene_input_pending() != 0 ||
        !scene_thread_create(input_worker, &probe, &id)) {
        return false;
    }
    const struct scene_pointer_event buffered = {2, -1, 0, 0, 0};
    const uint64_t state = scene_arch_interrupts_save();
    const bool queued = scene_input_push_pointer(&buffered);
    scene_arch_interrupts_restore(state);
    if (!queued || !scene_thread_yield() || probe.failed || probe.count != 1U) {
        return false;
    }
    static const size_t targets[] = {3U, 4U, 5U, 6U, 7U};
    for (size_t step = 0; step < sizeof(targets) / sizeof(targets[0]); ++step) {
        /* Emit only after the worker is BLOCKED. No root-side wake occurs. */
        if (!has_state(id, SCENE_THREAD_BLOCKED)) {
            return false;
        }
        scene_log_u64("Waiting for thread input step: ", step);
        const uint64_t start = scene_clock_monotonic_ns();
        while (probe.count < targets[step]) {
            if (probe.failed || scene_clock_monotonic_ns() - start >
                UINT64_C(2000000000) || !scene_thread_yield()) {
                return false;
            }
        }
        if (probe.count != targets[step]) {
            return false;
        }
    }
    if (probe.failed || !has_state(id, SCENE_THREAD_EXITED) ||
        !scene_thread_reap(id) || scene_thread_count() != 1U ||
        scene_input_pending() != 0 || scene_input_dropped() != 0) {
        return false;
    }
    scene_log_line("Input worker blocked before IRQ injection: verified");
    scene_log_u64("Blocking input events consumed: ", probe.count);
    scene_log_u64("Input events dropped: ", scene_input_dropped());
    scene_log_line("IRQ-to-thread input wakeups: verified");
    return true;
}

bool scene_thread_wait_self_test(void) {
    return verify_wait_queue() && verify_input_wait();
}
