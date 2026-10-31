bits 64
default rel

section .text
global scene_arch_context_switch
scene_arch_context_switch:
    ; System V call boundary: caller-saved GPRs are already the caller's
    ; responsibility. IF is clear; FPU/SIMD and address spaces are not switched.
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
