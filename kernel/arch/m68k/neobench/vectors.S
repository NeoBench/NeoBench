.section .text

/*
 * Initial 68060 exception-vector table for the NeoBench machine port.
 *
 * This table is deliberately self-contained.  The final FreeBSD trap-frame
 * glue will replace the common stub once the m68k machine ABI is wired in.
 */
.global neobench_vector_table
.global neobench_vector_default

.extern neobench_exception

.macro VECTOR_HANDLER n
neobench_vector_\n:
    moveq   #\n,%d0
    jmp     neobench_exception
.endm

neobench_vector_default:
    moveq   #0,%d0
    jmp     neobench_exception

VECTOR_HANDLER 2
VECTOR_HANDLER 3
VECTOR_HANDLER 4
VECTOR_HANDLER 5
VECTOR_HANDLER 6
VECTOR_HANDLER 7
VECTOR_HANDLER 8
VECTOR_HANDLER 9
VECTOR_HANDLER 10
VECTOR_HANDLER 11
VECTOR_HANDLER 12
VECTOR_HANDLER 13
VECTOR_HANDLER 14
VECTOR_HANDLER 15
VECTOR_HANDLER 16
VECTOR_HANDLER 17
VECTOR_HANDLER 18
VECTOR_HANDLER 19
VECTOR_HANDLER 20
VECTOR_HANDLER 21
VECTOR_HANDLER 22
VECTOR_HANDLER 23
VECTOR_HANDLER 24
VECTOR_HANDLER 25
VECTOR_HANDLER 26
VECTOR_HANDLER 27
VECTOR_HANDLER 28
VECTOR_HANDLER 29
VECTOR_HANDLER 30
VECTOR_HANDLER 31

.section .data
.align 4
neobench_vector_table:
    .long 0
    .long neobench_vector_2
    .long neobench_vector_3
    .long neobench_vector_4
    .long neobench_vector_5
    .long neobench_vector_6
    .long neobench_vector_7
    .long neobench_vector_8
    .long neobench_vector_9
    .long neobench_vector_10
    .long neobench_vector_11
    .long neobench_vector_12
    .long neobench_vector_13
    .long neobench_vector_14
    .long neobench_vector_15
    .long neobench_vector_16
    .long neobench_vector_17
    .long neobench_vector_18
    .long neobench_vector_19
    .long neobench_vector_20
    .long neobench_vector_21
    .long neobench_vector_22
    .long neobench_vector_23
    .long neobench_vector_24
    .long neobench_vector_25
    .long neobench_vector_26
    .long neobench_vector_27
    .long neobench_vector_28
    .long neobench_vector_29
    .long neobench_vector_30
    .long neobench_vector_31
