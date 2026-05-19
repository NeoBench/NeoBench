        cpu 68000

        org $7C00

start:

        lea $20000,a0
        lea kernel(pc),a1

copy_loop:
        move.b (a1)+,(a0)+
        cmpa.l #kernel_end,a1
        bne copy_loop

        jmp $20000

kernel:

kernel_end:

