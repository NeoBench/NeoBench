    .section .text
    .globl _start

_start:
    lea     0x8200, %a7              | Setup stack (Chip RAM)

    jsr     init_aga                 | Initialize AGA registers
    jsr     draw_logo                | Display NeoBench logo
    jsr     play_sound               | Play boot sound

hang:
    bra     hang                     | Loop forever

init_aga:
    move.w  #$2200, $dff096          | BPLCON0: enable bitplanes, AGA mode
    move.w  #$0000, $dff108          | BPL1MOD: modulo = 0
    move.w  #$0000, $dff10A          | BPL2MOD: modulo = 0
    move.w  #1,     $dff0A0          | BPLCON2: 1 bitplane
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, $dff0e0             | BPL1PTH (Bitplane pointer)
    move.w  #40,  $dff108            | BPL1MOD (320 bytes/line)
    move.w  #$0001, $dff100          | BPLCON1: scroll
    move.w  #$0200, $dff096          | Enable DMA
    rts

play_sound:
    lea     boot_sound, %a0
    move.l  %a0, $dff0a4             | AUD0LCH
    move.w  #21*1024, $dff0a8        | AUD0LEN (approx. 1.3 sec @ 16kHz)
    move.w  #0x0020, $dff0aa         | AUD0PER (period ~16kHz)
    move.w  #0x10,   $dff0ac         | AUD0VOL (volume)
    move.w  #0x8200, $dff096         | DMA: audio + bitplanes
    rts

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
