.section .text
.global _start

_start:
    lea     main,%a0        # Load address of main() into A0
    jsr     (%a0)           # Jump to C entry point
    bra     .
