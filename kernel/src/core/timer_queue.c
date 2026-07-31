#include <scene/clock.h>
#include <scene/timer_queue.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct timer_entry {
    uint64_t deadline_ns;
    scene_timer_id id;
    scene_timer_callback callback;
    void *context;
};

static struct timer_entry timer_heap[SCENE_TIMER_QUEUE_CAPACITY];
static size_t timer_count;
static scene_timer_id next_timer_id;

static bool entry_before(
    const struct timer_entry *left,
    const struct timer_entry *right
) {
    return left->deadline_ns < right->deadline_ns ||
        (left->deadline_ns == right->deadline_ns &&
            left->id < right->id);
}

static void swap_entries(
    struct timer_entry *left,
    struct timer_entry *right
) {
    const struct timer_entry temporary = *left;
    *left = *right;
    *right = temporary;
}

static void sift_up(size_t index) {
    while (index != 0) {
        const size_t parent = (index - 1U) / 2U;
        if (!entry_before(&timer_heap[index], &timer_heap[parent])) {
            break;
        }
        swap_entries(&timer_heap[index], &timer_heap[parent]);
        index = parent;
    }
}

static void sift_down(size_t index) {
    for (;;) {
        const size_t left = index * 2U + 1U;
        const size_t right = left + 1U;
        size_t earliest = index;
        if (left < timer_count &&
            entry_before(&timer_heap[left], &timer_heap[earliest])) {
            earliest = left;
        }
        if (right < timer_count &&
            entry_before(&timer_heap[right], &timer_heap[earliest])) {
            earliest = right;
        }
        if (earliest == index) {
            return;
        }
        swap_entries(&timer_heap[index], &timer_heap[earliest]);
        index = earliest;
    }
}

static struct timer_entry remove_at(size_t index) {
    const struct timer_entry removed = timer_heap[index];
    --timer_count;
    if (index != timer_count) {
        timer_heap[index] = timer_heap[timer_count];
        if (index != 0 &&
            entry_before(
                &timer_heap[index],
                &timer_heap[(index - 1U) / 2U]
            )) {
            sift_up(index);
        } else {
            sift_down(index);
        }
    }
    return removed;
}

void scene_timer_queue_init(void) {
    timer_count = 0;
    next_timer_id = 1;
}

bool scene_timer_schedule_at(
    uint64_t deadline_ns,
    scene_timer_callback callback,
    void *context,
    scene_timer_id *timer_id
) {
    if (!scene_clock_ready() || callback == 0 || timer_id == 0 ||
        timer_count >= SCENE_TIMER_QUEUE_CAPACITY) {
        return false;
    }

    scene_timer_id id = next_timer_id;
    ++next_timer_id;
    if (id == 0) {
        id = next_timer_id;
        ++next_timer_id;
    }
    timer_heap[timer_count] = (struct timer_entry){
        .deadline_ns = deadline_ns,
        .id = id,
        .callback = callback,
        .context = context
    };
    *timer_id = id;
    sift_up(timer_count);
    ++timer_count;
    return true;
}

bool scene_timer_schedule_after(
    uint64_t delay_ns,
    scene_timer_callback callback,
    void *context,
    scene_timer_id *timer_id
) {
    const uint64_t now = scene_clock_monotonic_ns();
    if (!scene_clock_ready() || delay_ns > UINT64_MAX - now) {
        return false;
    }
    return scene_timer_schedule_at(
        now + delay_ns,
        callback,
        context,
        timer_id
    );
}

bool scene_timer_cancel(scene_timer_id timer_id) {
    if (timer_id == 0) {
        return false;
    }
    for (size_t index = 0; index < timer_count; ++index) {
        if (timer_heap[index].id == timer_id) {
            (void)remove_at(index);
            return true;
        }
    }
    return false;
}

size_t scene_timer_run_due(void) {
    size_t callbacks_run = 0;
    while (timer_count != 0 &&
        timer_heap[0].deadline_ns <= scene_clock_monotonic_ns()) {
        const struct timer_entry entry = remove_at(0);
        entry.callback(entry.context);
        ++callbacks_run;
    }
    return callbacks_run;
}

size_t scene_timer_pending(void) {
    return timer_count;
}
