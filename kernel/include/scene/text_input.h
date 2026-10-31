#ifndef SCENE_TEXT_INPUT_H
#define SCENE_TEXT_INPUT_H

#include <scene/input.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum scene_keyboard_layout {
    SCENE_KEYBOARD_LAYOUT_US = 1
};

struct scene_text_event {
    uint64_t source_sequence;
    uint32_t codepoint;
    bool repeat;
};

/* Consumer-side translation; no queue access or persistent modifier state.
 * False means no text; the caller's output remains unchanged. */
bool scene_text_input_translate(
    enum scene_keyboard_layout layout,
    const struct scene_input_event *input,
    struct scene_text_event *text
);

/* Encode one Unicode scalar, without a terminator. Returns 1-4 bytes, or
 * zero without writing if the scalar, buffer, or capacity is invalid. */
size_t scene_text_input_encode_utf8(
    uint32_t codepoint, uint8_t *buffer, size_t capacity
);

#endif
