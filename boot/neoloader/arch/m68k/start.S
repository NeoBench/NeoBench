.section .text.start
.global _start
.extern neo_main
.extern __loader_bss_start
.extern __loader_bss_end

_start:
    /*
     * Establish a temporary supervisor stack.
     *
     * Final Amiga memory probing/placement will replace this
     * fixed stack in the platform-specific startup path.
     */
    lea     loader_stack_end,%sp

    /*
     * Clear NeoLoader BSS.
     */
    lea     __loader_bss_start,%a0
    lea     __loader_bss_end,%a1

1:
    cmp.l   %a1,%a0
    bcc.s   2f

    clr.l   (%a0)+
    bra.s   1b

2:
    jsr     neo_main

3:
    bra.s   3b

.section .bss
.align 16

loader_stack:
    .skip 8192

loader_stack_end:
