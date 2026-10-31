#include "text_input.h"

#include <scene/clock.h>
#include <scene/input.h>
#include <scene/log.h>
#include <scene/ps2_keyboard.h>
#include <scene/text_input.h>

static bool expect_translation(
    enum scene_keyboard_layout layout,
    const struct scene_input_event *input, uint32_t expected
) {
    struct scene_text_event text = {
        .source_sequence = UINT64_MAX,
        .codepoint = UINT32_MAX,
        .repeat = true
    };
    const bool produced = scene_text_input_translate(layout, input, &text);
    if (expected == 0) {
        return !produced && text.source_sequence == UINT64_MAX &&
            text.codepoint == UINT32_MAX && text.repeat;
    }
    return produced && text.source_sequence == input->sequence &&
        text.codepoint == expected && text.repeat == input->key.repeat;
}

static bool verify_layout(void) {
    static const enum scene_key_code alphabet[] = {
        SCENE_KEY_A, SCENE_KEY_B, SCENE_KEY_C, SCENE_KEY_D, SCENE_KEY_E,
        SCENE_KEY_F, SCENE_KEY_G, SCENE_KEY_H, SCENE_KEY_I, SCENE_KEY_J,
        SCENE_KEY_K, SCENE_KEY_L, SCENE_KEY_M, SCENE_KEY_N, SCENE_KEY_O,
        SCENE_KEY_P, SCENE_KEY_Q, SCENE_KEY_R, SCENE_KEY_S, SCENE_KEY_T,
        SCENE_KEY_U, SCENE_KEY_V, SCENE_KEY_W, SCENE_KEY_X, SCENE_KEY_Y,
        SCENE_KEY_Z
    };
    struct scene_input_event input = {
        .sequence = 37U,
        .type = SCENE_INPUT_EVENT_KEY,
        .key = {.code = SCENE_KEY_A, .pressed = true}
    };
    for (size_t letter = 0; letter < sizeof(alphabet) / sizeof(alphabet[0]);
        ++letter) {
        input.key.code = alphabet[letter];
        for (uint8_t modifiers = 0; modifiers < 16U; ++modifiers) {
            input.key.modifiers = modifiers;
            const bool upper =
                ((modifiers & SCENE_KEY_MODIFIER_SHIFT) != 0) !=
                ((modifiers & SCENE_KEY_MODIFIER_CAPS_LOCK) != 0);
            const uint32_t expected = (modifiers &
                (SCENE_KEY_MODIFIER_CONTROL | SCENE_KEY_MODIFIER_ALT)) != 0
                ? 0 : (upper ? 'A' : 'a') + (uint32_t)letter;
            if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, expected)) {
                return false;
            }
        }
    }
    static const enum scene_key_code symbols[] = {
        SCENE_KEY_0, SCENE_KEY_1, SCENE_KEY_2, SCENE_KEY_3, SCENE_KEY_4,
        SCENE_KEY_5, SCENE_KEY_6, SCENE_KEY_7, SCENE_KEY_8, SCENE_KEY_9,
        SCENE_KEY_MINUS, SCENE_KEY_EQUAL, SCENE_KEY_LEFT_BRACKET,
        SCENE_KEY_RIGHT_BRACKET, SCENE_KEY_BACKSLASH, SCENE_KEY_SEMICOLON,
        SCENE_KEY_APOSTROPHE, SCENE_KEY_GRAVE, SCENE_KEY_COMMA,
        SCENE_KEY_PERIOD, SCENE_KEY_SLASH, SCENE_KEY_SPACE
    };
    static const char plain[] = "0123456789-=[]\\;'`,./ ";
    static const char shifted[] = ")!@#$%^&*(_+{}|:\"~<>? ";
    _Static_assert(sizeof(plain) == sizeof(shifted), "symbol fixture mismatch");
    _Static_assert(sizeof(plain) - 1U == sizeof(symbols) / sizeof(symbols[0]),
        "symbol fixture size");
    for (size_t index = 0; index < sizeof(plain) - 1U; ++index) {
        input.key.code = symbols[index];
        input.key.modifiers = SCENE_KEY_MODIFIER_CAPS_LOCK;
        if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input,
                (uint32_t)plain[index])) {
            return false;
        }
        input.key.modifiers |= SCENE_KEY_MODIFIER_SHIFT;
        if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input,
                (uint32_t)shifted[index])) {
            return false;
        }
    }
    static const enum scene_key_code actions[] = {
        SCENE_KEY_UNKNOWN, SCENE_KEY_ESCAPE, SCENE_KEY_ENTER,
        SCENE_KEY_BACKSPACE, SCENE_KEY_TAB, SCENE_KEY_DELETE, SCENE_KEY_UP,
        SCENE_KEY_F1, SCENE_KEY_LEFT_SHIFT, SCENE_KEY_RIGHT_SHIFT,
        SCENE_KEY_CAPS_LOCK, SCENE_KEY_COUNT, (enum scene_key_code)-1
    };
    input.key.modifiers = 0;
    for (size_t index = 0; index < sizeof(actions) / sizeof(actions[0]); ++index) {
        input.key.code = actions[index];
        if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 0)) {
            return false;
        }
    }
    input.key.code = SCENE_KEY_A;
    input.key.repeat = true;
    input.sequence = UINT64_MAX;
    if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 'a')) {
        return false;
    }
    input.key.pressed = false;
    if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 0)) {
        return false;
    }
    input.key.pressed = true;
    input.key.modifiers = 0x80U;
    if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 0)) {
        return false;
    }
    input.key.modifiers = 0;
    if (!expect_translation((enum scene_keyboard_layout)0, &input, 0) ||
        !expect_translation((enum scene_keyboard_layout)-1, &input, 0) ||
        !expect_translation(SCENE_KEYBOARD_LAYOUT_US, 0, 0) ||
        scene_text_input_translate(SCENE_KEYBOARD_LAYOUT_US, &input, 0)) {
        return false;
    }
    input.type = SCENE_INPUT_EVENT_POINTER;
    if (!expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 0)) {
        return false;
    }
    input.type = (enum scene_input_event_type)0;
    return expect_translation(SCENE_KEYBOARD_LAYOUT_US, &input, 0);
}

static bool verify_utf8(void) {
    static const struct {
        uint32_t codepoint;
        uint8_t bytes[4];
        size_t length;
    } cases[] = {
        {0x0000U, {0x00}, 1U},
        {0x007fU, {0x7f}, 1U},
        {0x0080U, {0xc2, 0x80}, 2U},
        {0x07ffU, {0xdf, 0xbf}, 2U},
        {0x0800U, {0xe0, 0xa0, 0x80}, 3U},
        {0x20acU, {0xe2, 0x82, 0xac}, 3U},
        {0x4e2dU, {0xe4, 0xb8, 0xad}, 3U},
        {0xd7ffU, {0xed, 0x9f, 0xbf}, 3U},
        {0xe000U, {0xee, 0x80, 0x80}, 3U},
        {0xffffU, {0xef, 0xbf, 0xbf}, 3U},
        {0x10000U, {0xf0, 0x90, 0x80, 0x80}, 4U},
        {0x1f600U, {0xf0, 0x9f, 0x98, 0x80}, 4U},
        {0x10ffffU, {0xf4, 0x8f, 0xbf, 0xbf}, 4U},
        {0xd800U, {0}, 0},
        {0xdfffU, {0}, 0},
        {0x110000U, {0}, 0},
        {UINT32_MAX, {0}, 0}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        for (size_t capacity = 0; capacity <= 4U; ++capacity) {
            uint8_t buffer[6] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
            const size_t expected = capacity < cases[index].length
                ? 0 : cases[index].length;
            if (scene_text_input_encode_utf8(cases[index].codepoint,
                    &buffer[1], capacity) != expected) {
                return false;
            }
            for (size_t byte = 0; byte < sizeof(buffer); ++byte) {
                const uint8_t value = byte >= 1U && byte <= expected
                    ? cases[index].bytes[byte - 1U] : 0xa5U;
                if (buffer[byte] != value) {
                    return false;
                }
            }
        }
    }
    return scene_text_input_encode_utf8('a', 0, 4U) == 0;
}

static bool verify_injected_text(void) {
    /* Keep order in sync with the monitor script in scenectl.py. Each step
     * completes only after its primary key and all modifiers are released. */
    static const struct {
        enum scene_key_code key;
        uint8_t modifiers;
        uint32_t codepoint;
    } steps[] = {
        {SCENE_KEY_A, 0, 'a'},
        {SCENE_KEY_A, SCENE_KEY_MODIFIER_SHIFT, 'A'},
        {SCENE_KEY_CAPS_LOCK, SCENE_KEY_MODIFIER_CAPS_LOCK, 0},
        {SCENE_KEY_A, SCENE_KEY_MODIFIER_CAPS_LOCK, 'A'},
        {SCENE_KEY_A, SCENE_KEY_MODIFIER_SHIFT | SCENE_KEY_MODIFIER_CAPS_LOCK, 'a'},
        {SCENE_KEY_1, SCENE_KEY_MODIFIER_SHIFT | SCENE_KEY_MODIFIER_CAPS_LOCK, '!'},
        {SCENE_KEY_A, SCENE_KEY_MODIFIER_CONTROL | SCENE_KEY_MODIFIER_CAPS_LOCK, 0},
        {SCENE_KEY_A, SCENE_KEY_MODIFIER_ALT | SCENE_KEY_MODIFIER_CAPS_LOCK, 0},
        {SCENE_KEY_CAPS_LOCK, 0, 0},
        {SCENE_KEY_B, 0, 'b'},
        {SCENE_KEY_SPACE, 0, ' '},
        {SCENE_KEY_ENTER, 0, 0},
        {SCENE_KEY_BACKSPACE, 0, 0},
        {SCENE_KEY_UP, 0, 0},
        {SCENE_KEY_Z, SCENE_KEY_MODIFIER_SHIFT, 'Z'}
    };
    bool down[SCENE_KEY_COUNT] = {false};
    size_t held = 0;
    size_t text_count = 0;
    char result[9] = {0};
    uint64_t last_sequence = 0;
    for (size_t step = 0; step < sizeof(steps) / sizeof(steps[0]); ++step) {
        bool pressed = false;
        bool released = false;
        bool complete = false;
        size_t generated = 0;
        const uint64_t start = scene_clock_monotonic_ns();
        scene_log_u64("Waiting for text input step: ", step);
        while (!complete && scene_clock_monotonic_ns() - start <
            UINT64_C(2000000000)) {
            struct scene_input_event input;
            while (scene_input_pop(&input)) {
                if (input.sequence <= last_sequence) {
                    return false;
                }
                last_sequence = input.sequence;
                if (input.type != SCENE_INPUT_EVENT_KEY) {
                    continue;
                }
                const enum scene_key_code code = input.key.code;
                if (code <= SCENE_KEY_UNKNOWN || code >= SCENE_KEY_COUNT ||
                    input.key.repeat || input.key.pressed == down[code]) {
                    return false;
                }
                down[code] = input.key.pressed;
                if (input.key.pressed) {
                    ++held;
                } else {
                    --held;
                }
                if (code == steps[step].key) {
                    if (input.key.pressed) {
                        if (pressed || input.key.modifiers != steps[step].modifiers) {
                            return false;
                        }
                        pressed = true;
                    } else {
                        released = true;
                    }
                }
                struct scene_text_event text;
                if (scene_text_input_translate(SCENE_KEYBOARD_LAYOUT_US, &input,
                        &text)) {
                    if (text.codepoint != steps[step].codepoint ||
                        code != steps[step].key || text.repeat ||
                        text.source_sequence != input.sequence || generated != 0 ||
                        text_count >= sizeof(result) - 1U ||
                        scene_text_input_encode_utf8(text.codepoint,
                            (uint8_t *)&result[text_count], 1U) != 1U) {
                        return false;
                    }
                    ++generated;
                    ++text_count;
                }
                if (pressed && released && held == 0) {
                    complete = true;
                    break;
                }
            }
        }
        if (!complete || generated != (steps[step].codepoint != 0 ? 1U : 0U)) {
            scene_log_u64("Text input failed step: ", step);
            return false;
        }
    }
    scene_log_write("PS/2 text sequence: ");
    scene_log_line(result);
    scene_log_u64("PS/2 text event count: ", text_count);
    scene_log_u64("PS/2 scancodes received: ", scene_ps2_keyboard_scancodes());
    scene_log_u64("Input events dropped: ", scene_input_dropped());
    if (text_count != sizeof(result) - 1U || scene_input_dropped() != 0) {
        return false;
    }
    scene_log_line("Text key presses/releases and shortcuts: verified");
    return true;
}

bool scene_text_input_self_test(void) {
    if (!verify_layout()) {
        return false;
    }
    scene_log_line("Text layout edge cases: verified");
    if (!verify_utf8()) {
        return false;
    }
    scene_log_line("UTF-8 scalar boundaries: verified");
    if (!verify_injected_text()) {
        return false;
    }
    scene_log_line("PS/2 text input: verified");
    return true;
}
