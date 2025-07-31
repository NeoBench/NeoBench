.section .text
.globl _start

_start:
    lea     0x8200, %a7              | Setup stack

    jsr     init_aga                 | Init graphics
    jsr     draw_logo                | Show logo
    jsr     play_sound               | Boot sound

hang:
    bra     hang

init_aga:
    move.w  0x2200, 0xdff096        | BPLCON0
    move.w  0x0000, 0xdff108        | BPL1MOD
    move.w  0x0000, 0xdff10a        | BPL2MOD
    move.w  0x1,      0xdff0a0        | BPLCON2
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0             | BPL1PTH (Bitplane pointer)
    move.w  #40,  0xdff108            | BPL1MOD (320 bytes/line)
    move.w  #0001, 0xdff100          | BPLCON1: scroll
    move.w  #0200, 0xdff096          | Enable DMA
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, 0xdff0a4             | AUD0LCH
    move.w  #21*1024, 0xdff0a8        | AUD0LEN (approx. 1.3 sec @ 16kHz)
    move.w  #0x0020, 0xdff0aa         | AUD0PER (period ~16kHz)
    move.w  #0x10,   0xdff0ac         | AUD0VOL (volume)
    move.w  #0x8200, 0xdff096         | DMA: audio + bitplanes
    rts

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
