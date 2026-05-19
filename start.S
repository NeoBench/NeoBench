        .globl _start
        .globl kernel_main

_start:

        move.l  #0x40000,%sp

        jsr     kernel_main

.loop:
        bra.s   .loop
