/*
 * NeoBench
 * Motorola 680x0 Startup
 */

.section .text
.align 4

.global _start
.extern kernel_main

_start:

    /* Supervisor Mode */

    move.w  #0x2700,%sr

    /* TODO
       Setup Stack
       Clear BSS
       Setup MMU
       Detect CPU
    */

    jsr kernel_main

1:
    bra.s 1b

