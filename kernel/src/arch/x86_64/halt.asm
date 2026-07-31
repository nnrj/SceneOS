bits 64
section .text

global scene_arch_halt_forever

scene_arch_halt_forever:
    cli
.halt:
    hlt
    jmp .halt

