.section .text

/*
 * 68060 exception-vector table for the NeoBench MD port.
 *
 * The table is deliberately kept separate from the existing standalone
 * start.S. FreeBSD will own the final exception ABI once the m68k MD layer
 * is connected; these entries provide a stable platform boundary meanwhile.
 */
.global neobench_vector_table
.global neobench_default_exception
.extern neobench_exception_dispatch

.align 4
neobench_vector_table:
    .long 0
    .long neobench_reset
    .rept 254
    .long neobench_default_exception
    .endr

neobench_reset:
    bra neobench_default_exception

neobench_default_exception:
    movem.l %d0-%d7/%a0-%a6,-(%sp)
    jsr neobench_exception_dispatch
    movem.l (%sp)+,%d0-%d7/%a0-%a6
    rte
