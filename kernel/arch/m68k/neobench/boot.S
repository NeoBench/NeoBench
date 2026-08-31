.section .text

/*
 * NeoBench 68060 machine-entry scaffold.
 *
 * This is intentionally a small, freestanding entry boundary.  The existing
 * NeoBench start.S remains the reference bootstrap until the FreeBSD machine
 * vector/entry ABI is wired in.
 */
.global neobench_m68k_entry
.extern neobench_early_init
.extern neobench_kernel_entry

neobench_m68k_entry:
    /* Establish a known supervisor stack supplied by the linker/platform. */
    move.l  #neobench_boot_stack_top,%sp

    /* Save the incoming status register before platform setup. */
    move.w  %sr,%d0
    movem.l %d0-%d7/%a0-%a6,-(%sp)

    jsr     neobench_early_init
    jsr     neobench_kernel_entry

1:
    stop    #0x2700
    bra     1b

.section .bss
.align 4
neobench_boot_stack:
    .skip 16384
neobench_boot_stack_top:
