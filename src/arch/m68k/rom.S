    .section .text
    .globl _start
_start:
    move.l  #0x100000, %sp        /* Setup stack pointer */

    /* Enable DMA (bitplane) + display */
    move.w  #$8200, 0xdff096      /* DMACON: set DMAEN + BPLEN */
    move.w  #$0200, 0xdff100      /* BPLCON0: 1 bitplane, color mode */

    /* Point BPL1PT to Chip RAM at 0x00010000 */
    move.w  #0x0001, 0xdff0e0     /* BPL1PTH */
    move.w  #0x0000, 0xdff0e2     /* BPL1PTL */

    /* Fill bitplane memory (0x00010000) with white pixels */
    lea     0x00010000, %a0
    move.w  #5120, %d0
fill:
    move.l  #0xffffffff, (%a0)+
    dbra    %d0, fill

loop:
    bra loop
