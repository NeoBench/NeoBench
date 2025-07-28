    .section .text
    .global _start

_start:
    move.l #0x20000, %sp      /* Set up stack pointer */
    jsr main                  /* Call C main() */
loop:
    bra loop                  /* Hang */
