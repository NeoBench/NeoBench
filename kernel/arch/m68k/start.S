.section .text
.global _start
.extern kernel_main
.extern _bss_start
.extern _bss_end
.extern _stack_top

_start:
    /*
     * Establish the supervisor stack before calling C code.
     */
    move.l  #_stack_top,%sp

    /*
     * Clear .bss.
     */
    lea     _bss_start,%a0
    lea     _bss_end,%a1

clear_bss:
    cmpa.l  %a1,%a0
    bcc     bss_done

    clr.l   (%a0)+
    bra     clear_bss

bss_done:
    /*
     * Enter the C kernel.
     */
    jsr     kernel_main

/*
 * kernel_main() should never return.
 * If it does, halt permanently.
 */
halt:
    bra     halt


.section .bss

.align 4

_stack:
    .skip 16384

.align 4
.global _stack_top
_stack_top:
