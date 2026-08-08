.section .text
.global _start
.extern kernel_main

_start:

    move.l  #_stack_top, %sp

    lea     _bss_start, %a0
    lea     _bss_end, %a1

clear_bss:
    cmpa.l  %a1,%a0
    bge     bss_done

    clr.l   (%a0)+
    bra     clear_bss

bss_done:

    jsr kernel_main

halt:
    bra halt


.section .bss

.align 4
_stack:
    .skip 16384

_stack_top:
