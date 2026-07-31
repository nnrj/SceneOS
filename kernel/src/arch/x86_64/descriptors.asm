bits 64
default rel

section .text

global scene_arch_load_gdt
scene_arch_load_gdt:
    cli
    lgdt [rdi]

    push qword 0x08
    lea rax, [rel .reload_code_segment]
    push rax
    retfq

.reload_code_segment:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov ax, si
    ltr ax
    ret
