    .section .text
    .globl _start

_start:
    lea     0x00008200, %a7        | Setup stack in Chip RAM
    jsr     init_aga               | Init graphics
    jsr     draw_logo              | Display logo
    jsr     play_sound             | Play boot sound
.hang:
    bra     .hang                  | Loop forever

init_aga:
    move.w  #0x2200, 0xdff096         | DMACON: Enable bitplane DMA
    move.w  #0x0038, 0xdff08e         | DIWSTRT
    move.w  #0x00d0, 0xdff090         | DIWSTOP
    move.w  #0x0038, 0xdff092         | DDFSTRT
    move.w  #0x00d8, 0xdff094         | DDFSTOP
    move.w  #0x0001, 0xdff0a0         | BPLCON0: 1 bitplane, enable display
    move.w  #0x0020, 0xdff108         | BPL1MOD: line modulo
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0          | BPL1PTH
    move.w  #40, 0xdff108          | BPL1MOD (320 bytes/line)
    move.w  #1, 0xdff100           | BPLCON1: scroll
    move.w  #0x0200, 0xdff096      | BPLCON0: enable display again
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, 0xdff0a4           | AUD0LCH
    move.w  #0x5400, 0xdff0a8       | AUD0LEN (~21KB = 0x5400)
    move.w  #0x0020, 0xdff0aa       | AUD0PER: playback rate
    move.w  #0x0010, 0xdff0ac       | AUD0VOL
    move.w  #0x8200, 0xdff096	    | DMACON: enable audio + bitplane DMA
    rts

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"

    .globl _vectors
_vectors:
    .long 0x00008200                  | Stack pointer (SP at $00008200)
    .long _start                      | Program counter (entry point)
