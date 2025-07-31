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
    move.w  #0x000, 0xdff180          | COLOR00: black
    move.w  #0xFFF, 0xdff182          | COLOR01: white
    rts

init_video:
    move.w  #0x0200, 0xdff0a0         | BPLCON0: enable 1 bitplane
    move.w  #0x0000, 0xdff108         | BPL1MOD: modulo = 0
    move.w  #0x0000, 0xdff10a         | BPL2MOD: not used
    move.w  #0x0000, 0xdff100         | BPLCON1: scroll = 0
    move.w  #0x2200, 0xdff096         | DMACON: enable bitplane DMA
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0            | BPL1PTH (bitplane pointer)
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, 0xdff0a0            | AUD0LCH
    move.w  #0x5400,  0xdff0a4        | AUD0VOL: volume
    move.w  #0x0020,  0xdff0a8        | AUD0LEN: length
    move.w  #0x0010,  0xdff0aa        | AUD0PER: period (~8kHz)
    move.w  #0x8200,  0xdff096        | DMACON: enable audio DMA
    rts

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
