.section .text
    .globl _start

_start:
    lea     0x8200, %a7              | Setup stack in Chip RAM

    jsr     set_palette              | Set color palette
    jsr     init_video               | Enable AGA display
    jsr     draw_logo                | Display logo
    jsr     play_sound               | Play boot sound

halt:
    bra halt                         | Infinite loop

set_palette:
    move.w  #0x000, 0xdff180         | COLOR00: black
    move.w  #0xFFF, 0xdff182         | COLOR01: white
    rts

init_video:
    move.w  #0x1200, 0xdff096        | DMACON: enable bitplane DMA first
    move.w  #0x0200, 0xdff0a0        | BPLCON0: enable 1 bitplane
    move.w  #0x0000, 0xdff108        | BPL1MOD: modulo = 0
    move.w  #0x0000, 0xdff10a        | BPL2MOD: not used
    move.w  #0x0000, 0xdff100        | BPLCON1: scroll = 0
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0            | BPL1PTH (bitplane pointer high)
    move.w  %a0, 0xdff0e2            | BPL1PTL (bitplane pointer low)
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, 0xdff0a0            | AUD0LCH (audio channel 0 location high)
    move.w  %a0, 0xdff0a2            | AUD0LCL (audio channel 0 location low)
    move.w  #2000, 0xdff0a4          | AUD0LEN: length in words (4000 bytes / 2)
    move.w  #0x00ff, 0xdff0a8        | AUD0PER: period for ~8kHz (PAL: 3546895/8000≈443)
    move.w  #64, 0xdff0a6            | AUD0VOL: volume (0-64)
    move.w  #0x8201, 0xdff096        | DMACON: enable audio channel 0 DMA + master
    rts

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
