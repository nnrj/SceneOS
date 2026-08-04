#ifndef SCENE_ARCH_H
#define SCENE_ARCH_H

#include <stdbool.h>
#include <stdint.h>

struct scene_boot_info;

bool scene_arch_irq_enable(uint32_t irq);
bool scene_arch_irq_disable(uint32_t irq);
bool scene_arch_hpet_init(const struct scene_boot_info *boot_info);
bool scene_arch_hpet_ready(void);
uint64_t scene_arch_hpet_counter(void);
uint64_t scene_arch_hpet_frequency_hz(void);
uint64_t scene_arch_timestamp_counter(void);
bool scene_arch_timestamp_counter_invariant(void);
_Noreturn void scene_arch_halt_forever(void);

#endif
