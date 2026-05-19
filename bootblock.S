    .section .text
    .global _start
_start:
    .ascii "DOS\0"
    .long 0
    .long 880

    /* GREEN - Loader active */
    move.l  #0xdff180, %a0
    move.w  #0x00f0, (%a0)

    movem.l %d0-%d7/%a0-%a6, -(%sp)
    
    /* Load Installer Kernel from Sector 2018 (offset within partition) */
    /* Physical Sector = PartitionStart(2016) + 2 = 2018 */
    /* Offset = 2018 * 512 = 1033216 */
    lea     0x10000.l, %a0
    move.l  #1033216, 0x2c(%a1) 
    move.l  #262144, 0x24(%a1)  /* Load 256KB */
    move.l  %a0, 0x20(%a1)
    move.w  #2, 0x1c(%a1)      /* CMD_READ */
    
    move.l  4.w, %a6
    jsr     -456(%a6)          /* DoIO */
    
    movem.l (%sp)+, %d0-%d7/%a0-%a6

    /* BLUE - Handover */
    move.l  #0xdff180, %a0
    move.w  #0x000f, (%a0)
    
    /* Jump directly to kernel reset PC (offset 4) */
    move.l  0x10004.l, %a0
    jmp     (%a0)
