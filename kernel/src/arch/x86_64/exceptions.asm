bits 64
default rel

section .rodata
global scene_exception_stubs
scene_exception_stubs:
%assign vector 0
%rep 32
    dq scene_exception_stub_%+vector
%assign vector vector + 1
%endrep

section .text

extern scene_arch_exception_dispatch

global scene_arch_load_idt
scene_arch_load_idt:
    lidt [rdi]
    ret

%macro EXCEPTION_NO_ERROR 1
global scene_exception_stub_%1
scene_exception_stub_%1:
    push qword 0
    push qword %1
    jmp scene_exception_common
%endmacro

%macro EXCEPTION_WITH_ERROR 1
global scene_exception_stub_%1
scene_exception_stub_%1:
    push qword %1
    jmp scene_exception_common
%endmacro

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_WITH_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_WITH_ERROR 10
EXCEPTION_WITH_ERROR 11
EXCEPTION_WITH_ERROR 12
EXCEPTION_WITH_ERROR 13
EXCEPTION_WITH_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_WITH_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_WITH_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_WITH_ERROR 29
EXCEPTION_WITH_ERROR 30
EXCEPTION_NO_ERROR 31

scene_exception_common:
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
    and rsp, -16
    call scene_arch_exception_dispatch

.halt:
    cli
    hlt
    jmp .halt
