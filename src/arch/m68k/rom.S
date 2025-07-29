    .section .text
    .globl _start

_start:
    lea     0x8200, %sp         | Set stack pointer (Chip RAM)
    jsr     init_aga            | Init AGA registers
    jsr     draw_logo           | Draw logo to screen
    jsr     play_boot_sound     | (optional) play boot sound
.hang:
    bra     .hang               | Infinite loop

init_aga:
    move.w  #0x0200, 0xdff096   | BPLCON0: enable bitplane DMA, HIRES
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0       | BPL1PTH: pointer to bitplane
    move.w  #40, 0xdff108       | BPL1MOD: modulo
    move.w  #0x0200, 0xdff096   | BPLCON0 again (redundant but safe)
    move.w  #1, 0xdff0a0        | BPLCON2: one bitplane
    rts

play_boot_sound:
    rts                         | <- placeholder if not implemented

    .section .data
    .globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"

    .globl boot_sound
boot_sound:
    .incbin "build/boot_sound.raw"
