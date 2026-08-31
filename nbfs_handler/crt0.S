/* AmigaOS loads the handler (LoadSeg) with stack and BSS ready; the
 * standard entry point just jumps into the C main loop.  handler_main
 * never returns. */
        .text
        .global _start
_start:
        jmp handler_main
        .global _main
_main:
        jmp handler_main

        .end