#include <scene/arch.h>
#include <scene/ps2_controller.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_PS2_DATA_PORT 0x60U
#define SCENE_PS2_STATUS_PORT 0x64U
#define SCENE_PS2_COMMAND_PORT 0x64U
#define SCENE_PS2_STATUS_INPUT_FULL (1U << 1U)
#define SCENE_PS2_DISABLE_FIRST_PORT 0xadU
#define SCENE_PS2_ENABLE_FIRST_PORT 0xaeU
#define SCENE_PS2_DISABLE_SECOND_PORT 0xa7U
#define SCENE_PS2_ENABLE_SECOND_PORT 0xa8U
#define SCENE_PS2_READ_CONFIGURATION 0x20U
#define SCENE_PS2_WRITE_CONFIGURATION 0x60U
#define SCENE_PS2_WRITE_SECOND_PORT 0xd4U
#define SCENE_PS2_CONFIG_FIRST_IRQ (1U << 0U)
#define SCENE_PS2_CONFIG_SECOND_IRQ (1U << 1U)
#define SCENE_PS2_CONFIG_FIRST_CLOCK_DISABLED (1U << 4U)
#define SCENE_PS2_CONFIG_SECOND_CLOCK_DISABLED (1U << 5U)
#define SCENE_PS2_CONFIG_TRANSLATION (1U << 6U)
#define SCENE_PS2_ACKNOWLEDGE 0xfaU
#define SCENE_PS2_RESEND 0xfeU
#define SCENE_PS2_WAIT_LIMIT 100000U

static uint8_t read_port8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void write_port8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t scene_ps2_status(void) {
    return read_port8(SCENE_PS2_STATUS_PORT);
}

uint8_t scene_ps2_read_data(void) {
    return read_port8(SCENE_PS2_DATA_PORT);
}

static bool wait_input_ready(void) {
    for (size_t attempt = 0; attempt < SCENE_PS2_WAIT_LIMIT; ++attempt) {
        if ((scene_ps2_status() & SCENE_PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
        __asm__ volatile ("pause");
    }
    return false;
}

static bool wait_output_ready(bool auxiliary) {
    for (size_t attempt = 0; attempt < SCENE_PS2_WAIT_LIMIT; ++attempt) {
        const uint8_t status = scene_ps2_status();
        if ((status & SCENE_PS2_STATUS_OUTPUT_FULL) != 0) {
            if (((status & SCENE_PS2_STATUS_AUXILIARY) != 0) == auxiliary) {
                return true;
            }
            (void)scene_ps2_read_data();
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
        if ((scene_ps2_status() & SCENE_PS2_STATUS_OUTPUT_FULL) == 0) {
            return;
        }
        (void)scene_ps2_read_data();
    }
}

bool scene_ps2_controller_init(void) {
    if (scene_arch_interrupts_enabled() ||
        !write_controller_command(SCENE_PS2_DISABLE_FIRST_PORT) ||
        !write_controller_command(SCENE_PS2_DISABLE_SECOND_PORT)) {
        return false;
    }
    flush_output();
    if (!write_controller_command(SCENE_PS2_ENABLE_FIRST_PORT) ||
        !write_controller_command(SCENE_PS2_ENABLE_SECOND_PORT) ||
        !write_controller_command(SCENE_PS2_READ_CONFIGURATION) ||
        !wait_output_ready(false)) {
        return false;
    }

    uint8_t configuration = scene_ps2_read_data();
    configuration |= SCENE_PS2_CONFIG_FIRST_IRQ |
        SCENE_PS2_CONFIG_SECOND_IRQ | SCENE_PS2_CONFIG_TRANSLATION;
    configuration &= (uint8_t)~(
        SCENE_PS2_CONFIG_FIRST_CLOCK_DISABLED |
        SCENE_PS2_CONFIG_SECOND_CLOCK_DISABLED
    );
    return write_controller_command(SCENE_PS2_WRITE_CONFIGURATION) &&
        write_data(configuration);
}

bool scene_ps2_device_command(
    enum scene_ps2_device device,
    uint8_t command
) {
    if (scene_arch_interrupts_enabled() ||
        (device != SCENE_PS2_DEVICE_KEYBOARD && device != SCENE_PS2_DEVICE_MOUSE)) {
        return false;
    }
    const bool auxiliary = device == SCENE_PS2_DEVICE_MOUSE;
    for (size_t attempt = 0; attempt < 3U; ++attempt) {
        uint8_t response;
        if ((auxiliary &&
            !write_controller_command(SCENE_PS2_WRITE_SECOND_PORT)) ||
            !write_data(command) || !scene_ps2_device_read(device, &response)) {
            return false;
        }
        if (response == SCENE_PS2_ACKNOWLEDGE) {
            return true;
        }
        if (response != SCENE_PS2_RESEND) {
            return false;
        }
    }
    return false;
}

bool scene_ps2_device_read(enum scene_ps2_device device, uint8_t *value) {
    if (value == 0 || scene_arch_interrupts_enabled() ||
        (device != SCENE_PS2_DEVICE_KEYBOARD && device != SCENE_PS2_DEVICE_MOUSE) ||
        !wait_output_ready(device == SCENE_PS2_DEVICE_MOUSE)) {
        return false;
    }
    *value = scene_ps2_read_data();
    return true;
}
