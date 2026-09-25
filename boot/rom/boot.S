/* NeoBench boot ROM header + entry point (m68k, kickstart-style layout). */
    .section .bootvec, "ax"
    .long 0x001FFFE0        /*  0: initial SSP: top of 2 MB chip ram */
    .long 0x00FC0000        /*  1: initial PC:  code area base in this ROM */

/*
 * Full 256-entry exception vector table.
 *
 * VBR stays 0, so vectors are fetched from address 0. At reset the ROM boot
 * overlay maps this image over the low 2 MB, so the table is fetched straight
 * out of the ROM. _start turns the overlay off and copies these 1024 bytes
 * into chip RAM, after which the same table is what the CPU reads.
 *
 * Every vector has to be filled: the old 64-entry table left vectors 64..255
 * as zero, so any of those exceptions dispatched to address 0, executed the
 * reset SSP word as code, faulted on the 0x00FC of the reset PC word at
 * address 4, and looped forever.
 */
    .long eh_buserr         /*  2: bus error            */
    .long eh_addrerr        /*  3: address error        */
    .long eh_illegal        /*  4: illegal instruction  */
    .long eh_divzero        /*  5: divide by zero       */
    .long eh_chk            /*  6: CHK                  */
    .long eh_trapv          /*  7: TRAPV                */
    .long eh_priv           /*  8: privilege violation  */
    .long eh_trace          /*  9: trace                */
    .long eh_linea          /* 10: line 1010 emulator   */
    .long eh_linef          /* 11: line 1111 emulator   */
    .rept 52                /* 12..63: reserved / TRAP #n   */
    .long eh_rng_lo
    .endr
    .rept 64                /* 64..127                  */
    .long eh_rng_mid
    .endr
    .rept 64                /* 128..191                 */
    .long eh_rng_hi
    .endr
    .rept 64                /* 192..255                 */
    .long eh_rng_top
    .endr

    .section .text
    .global _start
    .extern rom_main
    .extern _bss_start
    .extern _bss_end

_start:
    /* Program the serial bit period before anything else. The emulator only
       starts transmitting once SERPER is written, so any output produced
       before this point is silently discarded. */
    move.w  #371, 0x00DFF032    /* SERPER: 9600 baud */

    /* Supervisor, everything masked, until the kernel decides otherwise. */
    move.w  #0x2700, %sr

    /*
     * Drop the ROM boot overlay (CIA A PRA bit 0).
     *
     * While the overlay is asserted the emulator maps this ROM over the whole
     * low 2 MB, mirrored every 512 KB. Chip RAM is therefore not readable or
     * writable there: the BSS clear, the frame buffer and -- fatally -- every
     * stack push are silently discarded, so the first `rts` popped the zero
     * padding at ROM offset $7FFDC and jumped to address 0.
     *
     * Kickstart does the same thing in ROMInit, just as the first thing after
     * the serial port is usable.
     */
    move.b  0x00BFE001, %d0
    andi.b  #0xfe, %d0
    move.b  %d0, 0x00BFE001

    /* Copy the vector table from ROM to address 0 (VBR never moves). */
    lea     0x00F80000, %a0
    lea     0x00000000, %a1
    move.w  #0x00FF, %d0        /* dbra: 256 longs = 1024 bytes */
1:
    move.l  (%a0)+, (%a1)+
    dbra    %d0, 1b

    /* Zero BSS, which lives in writable chip RAM (see rom.ld). */
    lea     _bss_start, %a0
    lea     _bss_end, %a1
2:
    cmpa.l  %a1, %a0
    bhs     3f
    clr.l   (%a0)+
    bra.s   2b
3:
    jsr     rom_main

4:
    bra.s   4b

/*
 * Fault stubs: write one byte naming the vector to SERDAT, then stop.
 *
 * No stack is touched -- the stub has to be usable when the stack is the
 * thing that broke -- and no `rte` is executed, so the capture file shows
 * exactly one byte for the first fault instead of an endless run of the
 * same character. The emulator flushes the pending byte at the next
 * horizontal blank even though the CPU never gets there again.
 *
 * 2..11 keep their vector number as a printable character; 12..255 are split
 * into four ranges so a stray vector is still identifiable in one byte.
 */
    .macro  EH name, ch
\name:
    move.w  #\ch, 0x00DFF030    /* SERDAT */
5:
    bra.s   5b
    .endm

    .align 4
    EH eh_buserr,   0x32        /* '2' */
    EH eh_addrerr,  0x33        /* '3' */
    EH eh_illegal,  0x34        /* '4' */
    EH eh_divzero,  0x35        /* '5' */
    EH eh_chk,      0x36        /* '6' */
    EH eh_trapv,    0x37        /* '7' */
    EH eh_priv,     0x38        /* '8' */
    EH eh_trace,    0x39        /* '9' */
    EH eh_linea,    0x41        /* 'A' */
    EH eh_linef,    0x46        /* 'F' */
    EH eh_rng_lo,   0x51        /* 'Q': vectors 12..63   */
    EH eh_rng_mid,  0x57        /* 'W': vectors 64..127  */
    EH eh_rng_hi,   0x45        /* 'E': vectors 128..191 */
    EH eh_rng_top,  0x52        /* 'R': vectors 192..255 */
