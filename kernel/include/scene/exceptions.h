#ifndef SCENE_EXCEPTIONS_H
#define SCENE_EXCEPTIONS_H

#include <stddef.h>
#include <stdint.h>

struct scene_exception_frame {
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

void scene_arch_exceptions_init(void);
void scene_arch_idt_set_gate(
    uint8_t vector,
    uintptr_t handler,
    uint8_t ist
);
_Noreturn void scene_arch_exception_dispatch(
    const struct scene_exception_frame *frame
);

#endif
