#include <scene/timer.h>

#include <stdint.h>

#define SCENE_PIT_INPUT_HZ 1193182U
#define SCENE_PIT_CHANNEL0 0x40U
#define SCENE_PIT_COMMAND 0x43U
#define SCENE_PIT_RATE_GENERATOR 0x36U

extern bool scene_arch_irq_unmask(uint8_t irq);

static volatile uint64_t elapsed_ticks;
static uint32_t configured_frequency;

static void write_port8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

bool scene_timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0U || frequency_hz > SCENE_PIT_INPUT_HZ) {
        return false;
    }

    uint32_t divisor = SCENE_PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0U || divisor > UINT16_MAX) {
        return false;
    }

    configured_frequency = SCENE_PIT_INPUT_HZ / divisor;
    elapsed_ticks = 0;
    write_port8(SCENE_PIT_COMMAND, SCENE_PIT_RATE_GENERATOR);
    write_port8(SCENE_PIT_CHANNEL0, (uint8_t)(divisor & UINT8_MAX));
    write_port8(SCENE_PIT_CHANNEL0, (uint8_t)(divisor >> 8U));
    return scene_arch_irq_unmask(0);
}

uint32_t scene_timer_frequency(void) {
    return configured_frequency;
}

uint64_t scene_timer_ticks(void) {
    return elapsed_ticks;
}

void scene_timer_handle_tick(void) {
    ++elapsed_ticks;
}

static uint64_t read_timestamp_counter(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32U) | low;
}

bool scene_timer_wait_ticks(uint64_t tick_count) {
    const uint64_t initial_ticks = elapsed_ticks;
    const uint64_t watchdog_start = read_timestamp_counter();
    const uint64_t watchdog_cycles = UINT64_C(2000000000);
    while (elapsed_ticks - initial_ticks < tick_count) {
        __asm__ volatile ("pause" : : : "memory");
        if (read_timestamp_counter() - watchdog_start >
            watchdog_cycles) {
            return false;
        }
    }
    return true;
}
