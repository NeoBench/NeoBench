    .section .text
    .global _start
_start:
    .ascii "DOS\0"
    .long 0
    .long 880

    /* 1. Take over hardware */
    move.l  #0xdff000, %a0
    move.w  #0x7fff, 0x96(%a0)
    move.w  #0x7fff, 0x9a(%a0)
    
    /* 2. Visual: FLASH PURPLE */
    move.w  #0xf0f, 0x180(%a0)

    /* 3. Load Kernel from Sector 2048 */
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  #1048576, 0x2c(%a1) 
    move.l  #262144, 0x24(%a1)  
    move.l  #0x10000, %a2
    move.l  %a2, 0x20(%a1)
    move.w  #2, 0x1c(%a1)
    
    move.l  4.w, %a6
    jsr     -456(%a6)          /* DoIO */
    
    movem.l (%sp)+, %d0-%d7/%a0-%a6

    /* 4. Visual: FLASH WHITE */
    move.l  #0xdff180, %a0
    move.w  #0x0fff, (%a0)
    
    /* 5. Execute */
    move.l  0x10004.l, %a0
    jmp     (%a0)
