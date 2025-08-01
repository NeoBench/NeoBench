.section .text
    .globl _start

_start:
    lea     0x7FFE, %a7              | Setup stack (avoid conflict with custom chips)
    
    | Disable all interrupts and DMA first
    move.w  #0x7FFF, 0xdff09a        | INTENA: disable all interrupts
    move.w  #0x7FFF, 0xdff096        | DMACON: disable all DMA
    
    | Wait for raster to be in safe area
    move.w  #0x0020, %d0
wait_raster:
    cmp.w   0xdff004, %d0            | Wait for line 32
    bgt.s   wait_raster

    jsr     init_copper              | Setup copper list
    jsr     set_palette              | Set color palette  
    jsr     init_video               | Enable display
    jsr     draw_logo                | Display logo
    jsr     play_sound               | Play boot sound

halt:
    bra     halt                     | Infinite loop

init_copper:
    | Simple copper list to set basic display
    lea     copper_list, %a0
    move.l  %a0, 0xdff080            | COP1LCH: copper list pointer
    move.w  #0x8080, 0xdff096        | DMACON: enable copper DMA
    rts

set_palette:
    move.w  #0x000, 0xdff180         | COLOR00: black background
    move.w  #0xFFF, 0xdff182         | COLOR01: white foreground
    rts

init_video:
    | Set up display for 320x256 lowres
    move.w  #0x1200, 0xdff0a0        | BPLCON0: 1 bitplane, color enable
    move.w  #0x0000, 0xdff0a2        | BPLCON1: no scroll
    move.w  #0x0024, 0xdff0a4        | BPLCON2: sprites over playfield
    
    | Display window (standard PAL values)
    move.w  #0x2c81, 0xdff08e        | DIWSTRT: display window start
    move.w  #0x2cc1, 0xdff090        | DIWSTOP: display window stop
    move.w  #0x0038, 0xdff092        | DDFSTRT: data fetch start
    move.w  #0x00d0, 0xdff094        | DDFSTOP: data fetch stop
    
    | Bitplane modulo (40 bytes per line for 320 pixels)
    move.w  #0x0000, 0xdff108        | BPL1MOD: no modulo
    move.w  #0x0000, 0xdff10a        | BPL2MOD: not used
    
    | Enable bitplane DMA
    move.w  #0x8200, 0xdff096        | DMACON: enable bitplane DMA
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0            | BPL1PTH: bitplane 1 pointer high
    move.w  %a0, 0xdff0e2            | BPL1PTL: bitplane 1 pointer low
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, 0xdff0a0            | AUD0LCH: audio channel 0 location
    move.w  #2000, 0xdff0a4          | AUD0LEN: length in words
    move.w  #0x01AC, 0xdff0a6        | AUD0PER: period for ~8kHz (428 decimal)
    move.w  #64, 0xdff0a8            | AUD0VOL: maximum volume
    move.w  #0x8201, 0xdff096        | DMACON: enable audio channel 0
    rts

    .section .data
    
| Simple copper list for basic display
copper_list:
    .word   0x0100, 0x1200           | BPLCON0: 1 bitplane
    .word   0x0102, 0x0000           | BPLCON1: no scroll  
    .word   0x0104, 0x0024           | BPLCON2: normal priority
    .word   0x008e, 0x2c81           | DIWSTRT: display start
    .word   0x0090, 0x2cc1           | DIWSTOP: display stop
    .word   0x0092, 0x0038           | DDFSTRT: data fetch start
    .word   0x0094, 0x00d0           | DDFSTOP: data fetch stop
    .word   0x0180, 0x0000           | COLOR00: black
    .word   0x0182, 0x0fff           | COLOR01: white
    .word   0xFFFF, 0xFFFE           | End copper list

    .align  4                        | Align to 32-bit boundary
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .align  4                        | Align to 32-bit boundary  
    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
