#ifndef SCENE_INTERRUPTS_H
#define SCENE_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

struct scene_boot_info;

struct scene_interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
};

void scene_arch_interrupts_init(void);
bool scene_arch_interrupts_activate_apic(
    const struct scene_boot_info *boot_info
);
const char *scene_arch_interrupt_controller(void);
uint32_t scene_arch_local_apic_id(void);
uint32_t scene_arch_timer_gsi(void);
void scene_arch_interrupts_enable(void);
void scene_arch_interrupts_disable(void);
void scene_arch_interrupt_dispatch(
    const struct scene_interrupt_frame *frame
);

#endif
