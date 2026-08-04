#include <scene/arch.h>
#include <scene/irq.h>
#include <scene/timer.h>

#include <stdint.h>

#define SCENE_PIT_INPUT_HZ 1193182U
#define SCENE_PIT_CHANNEL0 0x40U
#define SCENE_PIT_COMMAND 0x43U
#define SCENE_PIT_RATE_GENERATOR 0x36U

static volatile uint64_t elapsed_ticks;
static uint32_t configured_frequency;

static void write_port8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void handle_timer_irq(uint32_t irq, void *context) {
    (void)irq;
    (void)context;
    ++elapsed_ticks;
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
    return scene_irq_attach(0, handle_timer_irq, 0);
}

uint32_t scene_timer_frequency(void) {
    return configured_frequency;
}

uint64_t scene_timer_ticks(void) {
    return elapsed_ticks;
}

uint64_t scene_arch_timestamp_counter(void) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32U) | low;
}

bool scene_arch_timestamp_counter_invariant(void) {
    uint32_t eax = 0x80000000U;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile (
        "cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    );
    if (eax < 0x80000007U) {
        return false;
    }
    eax = 0x80000007U;
    __asm__ volatile (
        "cpuid"
        : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
    );
    (void)ebx;
    (void)ecx;
    return (edx & (1U << 8U)) != 0;
}

bool scene_timer_wait_ticks(uint64_t tick_count) {
    const uint64_t initial_ticks = elapsed_ticks;
    const uint64_t watchdog_start = scene_arch_timestamp_counter();
    const uint64_t watchdog_cycles = UINT64_C(2000000000);
    while (elapsed_ticks - initial_ticks < tick_count) {
        __asm__ volatile ("pause" : : : "memory");
        if (scene_arch_timestamp_counter() - watchdog_start >
            watchdog_cycles) {
            return false;
        }
    }
    return true;
}
