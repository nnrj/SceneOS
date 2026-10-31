#include <scene/text_input.h>

struct key_symbols {
    uint32_t plain;
    uint32_t shifted;
};

static const struct key_symbols us_symbols[SCENE_KEY_COUNT] = {
    [SCENE_KEY_1] = {'1', '!'},
    [SCENE_KEY_2] = {'2', '@'},
    [SCENE_KEY_3] = {'3', '#'},
    [SCENE_KEY_4] = {'4', '$'},
    [SCENE_KEY_5] = {'5', '%'},
    [SCENE_KEY_6] = {'6', '^'},
    [SCENE_KEY_7] = {'7', '&'},
    [SCENE_KEY_8] = {'8', '*'},
    [SCENE_KEY_9] = {'9', '('},
    [SCENE_KEY_0] = {'0', ')'},
    [SCENE_KEY_MINUS] = {'-', '_'},
    [SCENE_KEY_EQUAL] = {'=', '+'},
    [SCENE_KEY_Q] = {'q', 'Q'},
    [SCENE_KEY_W] = {'w', 'W'},
    [SCENE_KEY_E] = {'e', 'E'},
    [SCENE_KEY_R] = {'r', 'R'},
    [SCENE_KEY_T] = {'t', 'T'},
    [SCENE_KEY_Y] = {'y', 'Y'},
    [SCENE_KEY_U] = {'u', 'U'},
    [SCENE_KEY_I] = {'i', 'I'},
    [SCENE_KEY_O] = {'o', 'O'},
    [SCENE_KEY_P] = {'p', 'P'},
    [SCENE_KEY_LEFT_BRACKET] = {'[', '{'},
    [SCENE_KEY_RIGHT_BRACKET] = {']', '}'},
    [SCENE_KEY_A] = {'a', 'A'},
    [SCENE_KEY_S] = {'s', 'S'},
    [SCENE_KEY_D] = {'d', 'D'},
    [SCENE_KEY_F] = {'f', 'F'},
    [SCENE_KEY_G] = {'g', 'G'},
    [SCENE_KEY_H] = {'h', 'H'},
    [SCENE_KEY_J] = {'j', 'J'},
    [SCENE_KEY_K] = {'k', 'K'},
    [SCENE_KEY_L] = {'l', 'L'},
    [SCENE_KEY_SEMICOLON] = {';', ':'},
    [SCENE_KEY_APOSTROPHE] = {'\'', '"'},
    [SCENE_KEY_GRAVE] = {'`', '~'},
    [SCENE_KEY_BACKSLASH] = {'\\', '|'},
    [SCENE_KEY_Z] = {'z', 'Z'},
    [SCENE_KEY_X] = {'x', 'X'},
    [SCENE_KEY_C] = {'c', 'C'},
    [SCENE_KEY_V] = {'v', 'V'},
    [SCENE_KEY_B] = {'b', 'B'},
    [SCENE_KEY_N] = {'n', 'N'},
    [SCENE_KEY_M] = {'m', 'M'},
    [SCENE_KEY_COMMA] = {',', '<'},
    [SCENE_KEY_PERIOD] = {'.', '>'},
    [SCENE_KEY_SLASH] = {'/', '?'},
    [SCENE_KEY_SPACE] = {' ', ' '}
};

bool scene_text_input_translate(
    enum scene_keyboard_layout layout,
    const struct scene_input_event *input,
    struct scene_text_event *text
) {
    if (layout != SCENE_KEYBOARD_LAYOUT_US || input == 0 || text == 0 ||
        input->type != SCENE_INPUT_EVENT_KEY || !input->key.pressed ||
        input->key.code <= SCENE_KEY_UNKNOWN ||
        input->key.code >= SCENE_KEY_COUNT) {
        return false;
    }
    const uint8_t supported_modifiers = SCENE_KEY_MODIFIER_SHIFT |
        SCENE_KEY_MODIFIER_CONTROL | SCENE_KEY_MODIFIER_ALT |
        SCENE_KEY_MODIFIER_CAPS_LOCK;
    if ((input->key.modifiers & (uint8_t)~supported_modifiers) != 0 ||
        (input->key.modifiers &
            (SCENE_KEY_MODIFIER_CONTROL | SCENE_KEY_MODIFIER_ALT)) != 0) {
        return false;
    }
    const struct key_symbols symbols = us_symbols[input->key.code];
    if (symbols.plain == 0) {
        return false;
    }
    bool shifted = (input->key.modifiers & SCENE_KEY_MODIFIER_SHIFT) != 0;
    if (symbols.plain >= 'a' && symbols.plain <= 'z' &&
        (input->key.modifiers & SCENE_KEY_MODIFIER_CAPS_LOCK) != 0) {
        shifted = !shifted;
    }
    *text = (struct scene_text_event){
        .source_sequence = input->sequence,
        .codepoint = shifted ? symbols.shifted : symbols.plain,
        .repeat = input->key.repeat
    };
    return true;
}

size_t scene_text_input_encode_utf8(
    uint32_t codepoint, uint8_t *buffer, size_t capacity
) {
    if (buffer == 0 || codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff))) {
        return 0;
    }
    uint8_t encoded[4];
    size_t length;
    if (codepoint <= UINT32_C(0x7f)) {
        length = 1U;
        encoded[0] = (uint8_t)codepoint;
    } else if (codepoint <= UINT32_C(0x7ff)) {
        length = 2U;
        encoded[0] = (uint8_t)(0xc0U | (codepoint >> 6U));
        encoded[1] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    } else if (codepoint <= UINT32_C(0xffff)) {
        length = 3U;
        encoded[0] = (uint8_t)(0xe0U | (codepoint >> 12U));
        encoded[1] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3fU));
        encoded[2] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    } else {
        length = 4U;
        encoded[0] = (uint8_t)(0xf0U | (codepoint >> 18U));
        encoded[1] = (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3fU));
        encoded[2] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3fU));
        encoded[3] = (uint8_t)(0x80U | (codepoint & 0x3fU));
    }
    if (capacity < length) {
        return 0;
    }
    for (size_t index = 0; index < length; ++index) {
        buffer[index] = encoded[index];
    }
    return length;
}
