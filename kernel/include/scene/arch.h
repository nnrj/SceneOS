#ifndef SCENE_ARCH_H
#define SCENE_ARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct scene_boot_info;

/* Architecture-owned continuation; core code must not inspect its contents. */
struct scene_arch_context {
    uintptr_t saved_stack;
};

bool scene_arch_context_prepare(
    struct scene_arch_context *context,
    uintptr_t stack_bottom,
    size_t stack_size,
    void (*entry)(void)
);
/* Caller keeps IF clear across the switch; the resumed continuation restores it. */
void scene_arch_context_switch(
    struct scene_arch_context *previous, const struct scene_arch_context *next
);

bool scene_arch_irq_enable(uint32_t irq);
bool scene_arch_irq_disable(uint32_t irq);
uint64_t scene_arch_interrupts_save(void);
void scene_arch_interrupts_restore(uint64_t state);
bool scene_arch_interrupts_enabled(void);
bool scene_arch_local_timer_available(void);
bool scene_arch_local_timer_pending(void);
bool scene_arch_local_timer_configure(
    uint32_t initial_count, bool periodic, bool masked
);
uint32_t scene_arch_local_timer_current_count(void);
void scene_arch_local_timer_stop(void);
bool scene_arch_hpet_init(const struct scene_boot_info *boot_info);
bool scene_arch_hpet_ready(void);
uint64_t scene_arch_hpet_counter(void);
uint64_t scene_arch_hpet_frequency_hz(void);
uint64_t scene_arch_timestamp_counter(void);
bool scene_arch_timestamp_counter_invariant(void);
uint64_t scene_arch_timestamp_counter_frequency_hz(void);
_Noreturn void scene_arch_halt_forever(void);

#endif
