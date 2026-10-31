#ifndef SCENE_X86_64_APIC_H
#define SCENE_X86_64_APIC_H

#include <stdbool.h>
#include <stdint.h>

/* Keep the timer stub's pushed vector in interrupts.asm in sync. */
#define SCENE_LOCAL_TIMER_VECTOR 48U

struct scene_boot_info;

bool scene_arch_apic_init(const struct scene_boot_info *boot_info);
void scene_arch_apic_disable_for_pic(void);
bool scene_arch_apic_set_irq(uint8_t irq, uint8_t vector, bool masked);
void scene_arch_apic_eoi(void);
bool scene_arch_apic_ready(void);
uint32_t scene_arch_apic_id(void);
uint32_t scene_arch_apic_timer_gsi(void);

#endif
