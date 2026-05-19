.global _start

_start:

    lea 0x20000,%a0
    lea kernel,%a1

copy_loop:
    move.b (%a1)+,(%a0)+
    cmp.l #kernel_end,%a1
    bne copy_loop

    jmp 0x20000

kernel:

kernel_end:

