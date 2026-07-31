#include <scene/arch.h>
#include <scene/log.h>
#include <scene/serial.h>

#include <stddef.h>

static void write_digits(uint64_t value, uint32_t base) {
    static const char digits[] = "0123456789abcdef";
    char buffer[32];
    size_t count = 0;

    if (value == 0) {
        scene_serial_write_char('0');
        return;
    }

    while (value != 0 && count < sizeof(buffer)) {
        buffer[count] = digits[value % base];
        value /= base;
        ++count;
    }

    while (count != 0) {
        --count;
        scene_serial_write_char(buffer[count]);
    }
}

void scene_log_init(void) {
    (void)scene_serial_init();
}

void scene_log_write(const char *text) {
    scene_serial_write(text);
}

void scene_log_line(const char *text) {
    scene_serial_write(text);
    scene_serial_write("\n");
}

void scene_log_hex(const char *label, uint64_t value) {
    scene_serial_write(label);
    scene_serial_write("0x");
    write_digits(value, 16);
    scene_serial_write("\n");
}

void scene_log_u64(const char *label, uint64_t value) {
    scene_serial_write(label);
    write_digits(value, 10);
    scene_serial_write("\n");
}

_Noreturn void scene_panic(const char *reason) {
    scene_serial_write("\n[PANIC] ");
    scene_serial_write(reason);
    scene_serial_write("\n");
    scene_arch_halt_forever();
}

