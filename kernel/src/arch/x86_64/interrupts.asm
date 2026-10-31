bits 64
default rel

section .rodata
global scene_interrupt_stubs
scene_interrupt_stubs:
%assign irq 0
%rep 16
    dq scene_interrupt_stub_%+irq
%assign irq irq + 1
%endrep

section .text

extern scene_arch_interrupt_dispatch

%macro INTERRUPT_STUB 1
global scene_interrupt_stub_%1
scene_interrupt_stub_%1:
    push qword 0
    push qword (32 + %1)
    jmp scene_interrupt_common
%endmacro

%assign irq 0
%rep 16
INTERRUPT_STUB irq
%assign irq irq + 1
%endrep

; Keep vector 48 in sync with SCENE_LOCAL_TIMER_VECTOR in apic.h.
global scene_local_timer_stub
scene_local_timer_stub:
    push qword 0
    push qword 48
    jmp scene_interrupt_common

global scene_apic_spurious_stub
scene_apic_spurious_stub:
    push qword 0
    push qword 255
    jmp scene_interrupt_common

scene_interrupt_common:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    mov rax, rsp
    and rsp, -16
    sub rsp, 16
    mov [rsp], rax
    call scene_arch_interrupt_dispatch
    mov rsp, [rsp]

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq
