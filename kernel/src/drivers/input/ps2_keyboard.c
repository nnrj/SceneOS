#include <scene/input.h>
#include <scene/irq.h>
#include <scene/ps2_keyboard.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_PS2_DATA_PORT 0x60U
#define SCENE_PS2_STATUS_PORT 0x64U
#define SCENE_PS2_COMMAND_PORT 0x64U
#define SCENE_PS2_STATUS_OUTPUT_FULL (1U << 0U)
#define SCENE_PS2_STATUS_INPUT_FULL (1U << 1U)
#define SCENE_PS2_STATUS_AUXILIARY (1U << 5U)
#define SCENE_PS2_DISABLE_FIRST_PORT 0xadU
#define SCENE_PS2_ENABLE_FIRST_PORT 0xaeU
#define SCENE_PS2_READ_CONFIGURATION 0x20U
#define SCENE_PS2_WRITE_CONFIGURATION 0x60U
#define SCENE_PS2_CONFIG_FIRST_IRQ (1U << 0U)
#define SCENE_PS2_CONFIG_FIRST_CLOCK_DISABLED (1U << 4U)
#define SCENE_PS2_CONFIG_TRANSLATION (1U << 6U)
#define SCENE_PS2_KEYBOARD_ENABLE_SCANNING 0xf4U
#define SCENE_PS2_KEYBOARD_ACKNOWLEDGE 0xfaU
#define SCENE_PS2_KEYBOARD_RESEND 0xfeU
#define SCENE_PS2_KEYBOARD_IRQ 1U
#define SCENE_PS2_WAIT_LIMIT 100000U

static bool key_pressed[SCENE_KEY_COUNT];
static bool extended_scancode;
static bool left_shift;
static bool right_shift;
static bool left_control;
static bool right_control;
static bool left_alt;
static bool right_alt;
static bool caps_lock;
static volatile uint64_t received_scancodes;

static uint8_t read_port8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void write_port8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static bool wait_input_ready(void) {
    for (size_t attempt = 0; attempt < SCENE_PS2_WAIT_LIMIT; ++attempt) {
        if ((read_port8(SCENE_PS2_STATUS_PORT) &
            SCENE_PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static bool wait_output_ready(void) {
    for (size_t attempt = 0; attempt < SCENE_PS2_WAIT_LIMIT; ++attempt) {
        if ((read_port8(SCENE_PS2_STATUS_PORT) &
            SCENE_PS2_STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static bool write_controller_command(uint8_t command) {
    if (!wait_input_ready()) {
        return false;
    }
    write_port8(SCENE_PS2_COMMAND_PORT, command);
    return true;
}

static bool write_data(uint8_t value) {
    if (!wait_input_ready()) {
        return false;
    }
    write_port8(SCENE_PS2_DATA_PORT, value);
    return true;
}

static void flush_output(void) {
    for (size_t count = 0; count < 32U; ++count) {
        if ((read_port8(SCENE_PS2_STATUS_PORT) &
            SCENE_PS2_STATUS_OUTPUT_FULL) == 0) {
            return;
        }
        (void)read_port8(SCENE_PS2_DATA_PORT);
    }
}

static bool send_keyboard_command(uint8_t command) {
    for (size_t attempt = 0; attempt < 3U; ++attempt) {
        if (!write_data(command) || !wait_output_ready()) {
            return false;
        }
        const uint8_t response = read_port8(SCENE_PS2_DATA_PORT);
        if (response == SCENE_PS2_KEYBOARD_ACKNOWLEDGE) {
            return true;
        }
        if (response != SCENE_PS2_KEYBOARD_RESEND) {
            return false;
        }
    }
    return false;
}

static enum scene_key_code decode_standard(uint8_t code) {
    switch (code) {
        case 0x01: return SCENE_KEY_ESCAPE;
        case 0x02: return SCENE_KEY_1;
        case 0x03: return SCENE_KEY_2;
        case 0x04: return SCENE_KEY_3;
        case 0x05: return SCENE_KEY_4;
        case 0x06: return SCENE_KEY_5;
        case 0x07: return SCENE_KEY_6;
        case 0x08: return SCENE_KEY_7;
        case 0x09: return SCENE_KEY_8;
        case 0x0a: return SCENE_KEY_9;
        case 0x0b: return SCENE_KEY_0;
        case 0x0c: return SCENE_KEY_MINUS;
        case 0x0d: return SCENE_KEY_EQUAL;
        case 0x0e: return SCENE_KEY_BACKSPACE;
        case 0x0f: return SCENE_KEY_TAB;
        case 0x10: return SCENE_KEY_Q;
        case 0x11: return SCENE_KEY_W;
        case 0x12: return SCENE_KEY_E;
        case 0x13: return SCENE_KEY_R;
        case 0x14: return SCENE_KEY_T;
        case 0x15: return SCENE_KEY_Y;
        case 0x16: return SCENE_KEY_U;
        case 0x17: return SCENE_KEY_I;
        case 0x18: return SCENE_KEY_O;
        case 0x19: return SCENE_KEY_P;
        case 0x1a: return SCENE_KEY_LEFT_BRACKET;
        case 0x1b: return SCENE_KEY_RIGHT_BRACKET;
        case 0x1c: return SCENE_KEY_ENTER;
        case 0x1d: return SCENE_KEY_LEFT_CONTROL;
        case 0x1e: return SCENE_KEY_A;
        case 0x1f: return SCENE_KEY_S;
        case 0x20: return SCENE_KEY_D;
        case 0x21: return SCENE_KEY_F;
        case 0x22: return SCENE_KEY_G;
        case 0x23: return SCENE_KEY_H;
        case 0x24: return SCENE_KEY_J;
        case 0x25: return SCENE_KEY_K;
        case 0x26: return SCENE_KEY_L;
        case 0x27: return SCENE_KEY_SEMICOLON;
        case 0x28: return SCENE_KEY_APOSTROPHE;
        case 0x29: return SCENE_KEY_GRAVE;
        case 0x2a: return SCENE_KEY_LEFT_SHIFT;
        case 0x2b: return SCENE_KEY_BACKSLASH;
        case 0x2c: return SCENE_KEY_Z;
        case 0x2d: return SCENE_KEY_X;
        case 0x2e: return SCENE_KEY_C;
        case 0x2f: return SCENE_KEY_V;
        case 0x30: return SCENE_KEY_B;
        case 0x31: return SCENE_KEY_N;
        case 0x32: return SCENE_KEY_M;
        case 0x33: return SCENE_KEY_COMMA;
        case 0x34: return SCENE_KEY_PERIOD;
        case 0x35: return SCENE_KEY_SLASH;
        case 0x36: return SCENE_KEY_RIGHT_SHIFT;
        case 0x38: return SCENE_KEY_LEFT_ALT;
        case 0x39: return SCENE_KEY_SPACE;
        case 0x3a: return SCENE_KEY_CAPS_LOCK;
        case 0x3b: return SCENE_KEY_F1;
        case 0x3c: return SCENE_KEY_F2;
        case 0x3d: return SCENE_KEY_F3;
        case 0x3e: return SCENE_KEY_F4;
        case 0x3f: return SCENE_KEY_F5;
        case 0x40: return SCENE_KEY_F6;
        case 0x41: return SCENE_KEY_F7;
        case 0x42: return SCENE_KEY_F8;
        case 0x43: return SCENE_KEY_F9;
        case 0x44: return SCENE_KEY_F10;
        case 0x57: return SCENE_KEY_F11;
        case 0x58: return SCENE_KEY_F12;
        default: return SCENE_KEY_UNKNOWN;
    }
}

static enum scene_key_code decode_extended(uint8_t code) {
    switch (code) {
        case 0x1c: return SCENE_KEY_ENTER;
        case 0x1d: return SCENE_KEY_RIGHT_CONTROL;
        case 0x38: return SCENE_KEY_RIGHT_ALT;
        case 0x47: return SCENE_KEY_HOME;
        case 0x48: return SCENE_KEY_UP;
        case 0x49: return SCENE_KEY_PAGE_UP;
        case 0x4b: return SCENE_KEY_LEFT;
        case 0x4d: return SCENE_KEY_RIGHT;
        case 0x4f: return SCENE_KEY_END;
        case 0x50: return SCENE_KEY_DOWN;
        case 0x51: return SCENE_KEY_PAGE_DOWN;
        case 0x52: return SCENE_KEY_INSERT;
        case 0x53: return SCENE_KEY_DELETE;
        default: return SCENE_KEY_UNKNOWN;
    }
}

static uint8_t current_modifiers(void) {
    uint8_t modifiers = 0;
    if (left_shift || right_shift) {
        modifiers |= SCENE_KEY_MODIFIER_SHIFT;
    }
    if (left_control || right_control) {
        modifiers |= SCENE_KEY_MODIFIER_CONTROL;
    }
    if (left_alt || right_alt) {
        modifiers |= SCENE_KEY_MODIFIER_ALT;
    }
    if (caps_lock) {
        modifiers |= SCENE_KEY_MODIFIER_CAPS_LOCK;
    }
    return modifiers;
}

static void update_modifier(enum scene_key_code code, bool pressed) {
    switch (code) {
        case SCENE_KEY_LEFT_SHIFT: left_shift = pressed; break;
        case SCENE_KEY_RIGHT_SHIFT: right_shift = pressed; break;
        case SCENE_KEY_LEFT_CONTROL: left_control = pressed; break;
        case SCENE_KEY_RIGHT_CONTROL: right_control = pressed; break;
        case SCENE_KEY_LEFT_ALT: left_alt = pressed; break;
        case SCENE_KEY_RIGHT_ALT: right_alt = pressed; break;
        default: break;
    }
}

static void handle_scancode(uint8_t scancode) {
    ++received_scancodes;
    if (scancode == 0xe0U) {
        extended_scancode = true;
        return;
    }
    if (scancode == 0xe1U) {
        extended_scancode = false;
        return;
    }

    const bool pressed = (scancode & 0x80U) == 0;
    const uint8_t make_code = scancode & 0x7fU;
    const enum scene_key_code code = extended_scancode
        ? decode_extended(make_code)
        : decode_standard(make_code);
    extended_scancode = false;
    if (code == SCENE_KEY_UNKNOWN) {
        return;
    }

    const bool repeat = pressed && key_pressed[code];
    key_pressed[code] = pressed;
    update_modifier(code, pressed);
    if (code == SCENE_KEY_CAPS_LOCK && pressed && !repeat) {
        caps_lock = !caps_lock;
    }
    const struct scene_key_event event = {
        .code = code,
        .modifiers = current_modifiers(),
        .pressed = pressed,
        .repeat = repeat
    };
    (void)scene_input_push_key(&event);
}

static void keyboard_irq(uint32_t irq, void *context) {
    (void)irq;
    (void)context;
    for (size_t count = 0; count < 16U; ++count) {
        const uint8_t status = read_port8(SCENE_PS2_STATUS_PORT);
        if ((status & SCENE_PS2_STATUS_OUTPUT_FULL) == 0 ||
            (status & SCENE_PS2_STATUS_AUXILIARY) != 0) {
            return;
        }
        handle_scancode(read_port8(SCENE_PS2_DATA_PORT));
    }
}

bool scene_ps2_keyboard_init(void) {
    received_scancodes = 0;
    extended_scancode = false;
    left_shift = false;
    right_shift = false;
    left_control = false;
    right_control = false;
    left_alt = false;
    right_alt = false;
    caps_lock = false;
    for (size_t index = 0; index < SCENE_KEY_COUNT; ++index) {
        key_pressed[index] = false;
    }

    if (!write_controller_command(SCENE_PS2_DISABLE_FIRST_PORT)) {
        return false;
    }
    flush_output();
    if (!write_controller_command(SCENE_PS2_READ_CONFIGURATION) ||
        !wait_output_ready()) {
        return false;
    }
    uint8_t configuration = read_port8(SCENE_PS2_DATA_PORT);
    configuration |= SCENE_PS2_CONFIG_FIRST_IRQ |
        SCENE_PS2_CONFIG_TRANSLATION;
    configuration &= (uint8_t)~SCENE_PS2_CONFIG_FIRST_CLOCK_DISABLED;
    if (!write_controller_command(SCENE_PS2_WRITE_CONFIGURATION) ||
        !write_data(configuration) ||
        !write_controller_command(SCENE_PS2_ENABLE_FIRST_PORT) ||
        !send_keyboard_command(SCENE_PS2_KEYBOARD_ENABLE_SCANNING)) {
        return false;
    }
    return scene_irq_attach(SCENE_PS2_KEYBOARD_IRQ, keyboard_irq, 0);
}

uint64_t scene_ps2_keyboard_scancodes(void) {
    return received_scancodes;
}
