bits 64
default rel

section .text
extern scene_thread_yield
global scene_test_thread_registers

; Each worker supplies a distinct seed so cross-thread register leakage cannot
; pass merely because all workers happen to use identical canaries.
%macro SET_CANARY 2
    mov %1, %2
    xor %1, [rsp + 16]
%endmacro

%macro CHECK_CANARY 2
    mov r10, %2
    xor r10, [rsp + 16]
    cmp %1, r10
    jne .failed
%endmacro

scene_test_thread_registers:
    mov rax, rsp
    and eax, 15
    cmp eax, 8
    jne .unaligned
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 24
    mov qword [rsp], 64
    mov [rsp + 8], rsp
    mov [rsp + 16], rdi
.again:
    SET_CANARY rbp, 0x1122334455667788
    SET_CANARY rbx, 0x8877665544332211
    SET_CANARY r12, 0x123456789abcdef0
    SET_CANARY r13, 0xfedcba9876543210
    SET_CANARY r14, 0xa55a0ff05aa5f00f
    SET_CANARY r15, 0x55aa55aaaa55aa55
    call scene_thread_yield
    test al, al
    jz .failed
    cmp rsp, [rsp + 8]
    jne .failed
    CHECK_CANARY rbp, 0x1122334455667788
    CHECK_CANARY rbx, 0x8877665544332211
    CHECK_CANARY r12, 0x123456789abcdef0
    CHECK_CANARY r13, 0xfedcba9876543210
    CHECK_CANARY r14, 0xa55a0ff05aa5f00f
    CHECK_CANARY r15, 0x55aa55aaaa55aa55
    dec qword [rsp]
    jnz .again
    mov eax, 1
    jmp .restore
.failed:
    xor eax, eax
.restore:
    add rsp, 24
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
.unaligned:
    xor eax, eax
    ret
