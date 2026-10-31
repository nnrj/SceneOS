#ifndef SCENE_INPUT_H
#define SCENE_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_INPUT_QUEUE_CAPACITY 64U

enum scene_input_event_type {
    SCENE_INPUT_EVENT_KEY = 1,
    SCENE_INPUT_EVENT_POINTER
};

enum scene_pointer_buttons {
    SCENE_POINTER_BUTTON_LEFT = 1U << 0,
    SCENE_POINTER_BUTTON_RIGHT = 1U << 1,
    SCENE_POINTER_BUTTON_MIDDLE = 1U << 2
};

enum scene_key_code {
    SCENE_KEY_UNKNOWN = 0,
    SCENE_KEY_ESCAPE,
    SCENE_KEY_1,
    SCENE_KEY_2,
    SCENE_KEY_3,
    SCENE_KEY_4,
    SCENE_KEY_5,
    SCENE_KEY_6,
    SCENE_KEY_7,
    SCENE_KEY_8,
    SCENE_KEY_9,
    SCENE_KEY_0,
    SCENE_KEY_MINUS,
    SCENE_KEY_EQUAL,
    SCENE_KEY_BACKSPACE,
    SCENE_KEY_TAB,
    SCENE_KEY_Q,
    SCENE_KEY_W,
    SCENE_KEY_E,
    SCENE_KEY_R,
    SCENE_KEY_T,
    SCENE_KEY_Y,
    SCENE_KEY_U,
    SCENE_KEY_I,
    SCENE_KEY_O,
    SCENE_KEY_P,
    SCENE_KEY_LEFT_BRACKET,
    SCENE_KEY_RIGHT_BRACKET,
    SCENE_KEY_ENTER,
    SCENE_KEY_LEFT_CONTROL,
    SCENE_KEY_A,
    SCENE_KEY_S,
    SCENE_KEY_D,
    SCENE_KEY_F,
    SCENE_KEY_G,
    SCENE_KEY_H,
    SCENE_KEY_J,
    SCENE_KEY_K,
    SCENE_KEY_L,
    SCENE_KEY_SEMICOLON,
    SCENE_KEY_APOSTROPHE,
    SCENE_KEY_GRAVE,
    SCENE_KEY_LEFT_SHIFT,
    SCENE_KEY_BACKSLASH,
    SCENE_KEY_Z,
    SCENE_KEY_X,
    SCENE_KEY_C,
    SCENE_KEY_V,
    SCENE_KEY_B,
    SCENE_KEY_N,
    SCENE_KEY_M,
    SCENE_KEY_COMMA,
    SCENE_KEY_PERIOD,
    SCENE_KEY_SLASH,
    SCENE_KEY_RIGHT_SHIFT,
    SCENE_KEY_LEFT_ALT,
    SCENE_KEY_SPACE,
    SCENE_KEY_CAPS_LOCK,
    SCENE_KEY_F1,
    SCENE_KEY_F2,
    SCENE_KEY_F3,
    SCENE_KEY_F4,
    SCENE_KEY_F5,
    SCENE_KEY_F6,
    SCENE_KEY_F7,
    SCENE_KEY_F8,
    SCENE_KEY_F9,
    SCENE_KEY_F10,
    SCENE_KEY_F11,
    SCENE_KEY_F12,
    SCENE_KEY_RIGHT_CONTROL,
    SCENE_KEY_RIGHT_ALT,
    SCENE_KEY_HOME,
    SCENE_KEY_UP,
    SCENE_KEY_PAGE_UP,
    SCENE_KEY_LEFT,
    SCENE_KEY_RIGHT,
    SCENE_KEY_END,
    SCENE_KEY_DOWN,
    SCENE_KEY_PAGE_DOWN,
    SCENE_KEY_INSERT,
    SCENE_KEY_DELETE,
    SCENE_KEY_COUNT
};

enum scene_key_modifiers {
    SCENE_KEY_MODIFIER_SHIFT = 1U << 0,
    SCENE_KEY_MODIFIER_CONTROL = 1U << 1,
    SCENE_KEY_MODIFIER_ALT = 1U << 2,
    SCENE_KEY_MODIFIER_CAPS_LOCK = 1U << 3
};

struct scene_key_event {
    enum scene_key_code code;
    uint8_t modifiers;
    bool pressed;
    bool repeat;
};

struct scene_pointer_event {
    int32_t delta_x;
    int32_t delta_y;
    int32_t wheel_delta; /* Relative detents: positive up, negative down. */
    uint8_t buttons;
    uint8_t changed_buttons;
};

struct scene_input_event {
    uint64_t sequence;
    enum scene_input_event_type type;
    union {
        struct scene_key_event key;
        struct scene_pointer_event pointer;
    };
};

void scene_input_init(void);
bool scene_input_push_key(const struct scene_key_event *event);
bool scene_input_push_pointer(const struct scene_pointer_event *event);
bool scene_input_pop(struct scene_input_event *event);
size_t scene_input_pending(void);
uint64_t scene_input_dropped(void);

#endif
