#include <scene/input.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct scene_input_event event_queue[SCENE_INPUT_QUEUE_CAPACITY];
static volatile size_t read_index;
static volatile size_t write_index;
static uint64_t next_sequence;
static volatile uint64_t dropped_events;

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
    const size_t current_write = write_index;
    const size_t next_write =
        (current_write + 1U) % SCENE_INPUT_QUEUE_CAPACITY;
    if (next_write == read_index) {
        ++dropped_events;
        return false;
    }
    event_queue[current_write] = (struct scene_input_event){
        .sequence = next_sequence,
        .type = SCENE_INPUT_EVENT_KEY,
        .key = *event
    };
    ++next_sequence;
    write_index = next_write;
    return true;
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
