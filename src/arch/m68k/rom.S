    .section .text
    .globl _start

_start:
    jmp entry_point
    dc.l 0, 0  /* reserved, part of exec base */

entry_point:
    lea     0x8200, %a7        | Set up stack
    jsr     init_aga
    jsr     draw_logo
    bra     .                  | Loop forever

init_aga:
    move.w  #0x0200, 0xdff096   | Enable display, HIRES, BPLs
    rts

draw_logo:
    lea     neobench_logo, %a0
    move.l  %a0, 0xdff0e0      | BPL1PTH
    move.w  #40, 0xdff108      | BPL1MOD
    move.w  #1, 0xdff100       | BPLCON1
    move.w  #0x0200, 0xdff096   | BPLCON0 (enable)
    move.w  #0x0001, 0xdff0a0   | BPLCON2 (1 bitplane)
    rts

.section .data
.globl neobench_logo
neobench_logo:
    .incbin "build/neobench_logo.raw"
