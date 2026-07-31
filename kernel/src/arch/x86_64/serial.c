#include <scene/serial.h>

#include <stdint.h>

enum {
    SCENE_COM1 = 0x3f8,
    SCENE_LINE_STATUS = 5,
    SCENE_TRANSMITTER_EMPTY = 0x20
};

static bool serial_available;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

bool scene_serial_init(void) {
    out8(SCENE_COM1 + 1, 0x00);
    out8(SCENE_COM1 + 3, 0x80);
    out8(SCENE_COM1 + 0, 0x01);
    out8(SCENE_COM1 + 1, 0x00);
    out8(SCENE_COM1 + 3, 0x03);
    out8(SCENE_COM1 + 2, 0xc7);
    out8(SCENE_COM1 + 4, 0x0b);

    serial_available = (in8(SCENE_COM1 + SCENE_LINE_STATUS) != 0xff);
    return serial_available;
}

void scene_serial_write_char(char value) {
    if (!serial_available) {
        return;
    }

    while ((in8(SCENE_COM1 + SCENE_LINE_STATUS) & SCENE_TRANSMITTER_EMPTY) == 0) {
        __asm__ volatile ("pause");
    }

    out8(SCENE_COM1, (uint8_t)value);
}

void scene_serial_write(const char *text) {
    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            scene_serial_write_char('\r');
        }
        scene_serial_write_char(*text);
        ++text;
    }
}

