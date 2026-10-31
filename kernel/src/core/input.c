#include <scene/input.h>
#include <scene/thread.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct scene_input_event event_queue[SCENE_INPUT_QUEUE_CAPACITY];
static volatile size_t read_index;
static volatile size_t write_index;
static uint64_t next_sequence;
static volatile uint64_t dropped_events;
static struct scene_wait_queue input_waiters;

static bool push_event(const struct scene_input_event *event) {
    const size_t current_write = write_index;
    const size_t next_write =
        (current_write + 1U) % SCENE_INPUT_QUEUE_CAPACITY;
    if (next_write == read_index) {
        ++dropped_events;
        return false;
    }
    event_queue[current_write] = *event;
    event_queue[current_write].sequence = next_sequence;
    ++next_sequence;
    write_index = next_write;
    (void)scene_wait_queue_wake_one(&input_waiters);
    return true;
}

void scene_input_init(void) {
    read_index = 0;
    write_index = 0;
    next_sequence = 1;
    dropped_events = 0;
}

bool scene_input_push_key(const struct scene_key_event *event) {
    if (event == 0 || event->code <= SCENE_KEY_UNKNOWN ||
        event->code >= SCENE_KEY_COUNT) {
        return false;
    }
    const struct scene_input_event input_event = {
        .sequence = 0,
        .type = SCENE_INPUT_EVENT_KEY,
        .key = *event
    };
    return push_event(&input_event);
}

bool scene_input_push_pointer(const struct scene_pointer_event *event) {
    if (event == 0 ||
        (event->delta_x == 0 && event->delta_y == 0 &&
        event->wheel_delta == 0 && event->changed_buttons == 0)) {
        return false;
    }
    const struct scene_input_event input_event = {
        .sequence = 0,
        .type = SCENE_INPUT_EVENT_POINTER,
        .pointer = *event
    };
    return push_event(&input_event);
}

bool scene_input_pop(struct scene_input_event *event) {
    if (event == 0 || read_index == write_index) {
        return false;
    }
    const size_t current_read = read_index;
    *event = event_queue[current_read];
    read_index = (current_read + 1U) % SCENE_INPUT_QUEUE_CAPACITY;
    return true;
}

static bool input_available(void *context) {
    return scene_input_pop(context);
}

bool scene_input_wait(struct scene_input_event *event) {
    return event != 0 && scene_thread_wait(&input_waiters, input_available, event);
}

size_t scene_input_pending(void) {
    const size_t current_read = read_index;
    const size_t current_write = write_index;
    return current_write >= current_read
        ? current_write - current_read
        : SCENE_INPUT_QUEUE_CAPACITY - current_read + current_write;
}

uint64_t scene_input_dropped(void) {
    return dropped_events;
}
