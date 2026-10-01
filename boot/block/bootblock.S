/*
 * NeoBench chainload bootblock.
 *
 * The genuine AmigaOS 3.2.3 ROM scans bootable devices, loads sector 0
 * into chip RAM and jumps into it.  That hand-off is the whole point of
 * the chainload: NeoBench gets the machine from the ROM itself, with no
 * AmigaOS filesystem, no dos.library and no AmigaOS libraries in
 * between, so nothing that follows depends on AmigaOS being present.
 *
 * The first twelve bytes belong to the ROM, not to us:
 *
 *   0   'DOS',0   the magic the scan looks for before it runs anything
 *   4   checksum  over the whole 512-byte block, verified before we run
 *   8   entry     the root slot, executed if the ROM enters at +8
 *   12  code
 *
 * Only the checksum has to be honest: the ROM checks the sum but never
 * interprets the root slot, so that slot can hold code instead.  It
 * holds a branch forward, which makes both plausible entry points (+8
 * and +12) converge on +12 and guarantees the first instruction executed
 * is real code rather than a filesystem block number.
 *
 * This cut is a reconnaissance payload.  It reports where the ROM put
 * us, what it left in the registers, and whether SysBase survived -- all
 * over serial, the one channel still alive with no display, no
 * filesystem and no OS to draw anything.
 */

    .section .text
    .global _start

    .equ    SERDAT,   0x00dff030
    .equ    SERDATR,  0x00dff038
    .equ    SERPER,   0x00dff032
    .equ    STATMSK,  0x3000      /* TBE|TSRE, NDK hardware/custom.i     */

    .equ    NREGS,    17          /* 8 data + 7 address + sp + sr        */

_start:
    .ascii  "DOS"
    .byte   0
    .long   0                     /* checksum, filled in by mkboot.py    */
    .word   0x6002                /* bra.s +2 -> +12                     */
    .word   0x4e71                /* nop                                 */

entry:
    /*
     * Prove execution with the display before trusting the serial port.
     * COLOR00 is what the whole border and an empty frame are painted
     * from, so a byte here reaches the screen with no bitplanes, no
     * copper list of our own and no OS to draw anything -- which is
     * exactly the situation a bootblock runs in.  The shade advances as
     * the payload does, so one screenshot reports progress:
     *
     *   red    entered
     *   green  transmitter armed
     *   blue   diagnostics finished, spinning
     */
    move.w  #0x0f00, 0x00dff180

    /*
     * Arm the transmitter first.  SERPER is not reset to a usable value
     * and the emulator derives framing from it, so every byte written
     * before this lands at 256000 baud and is discarded uncaptured.
     */
    move.w  #371, SERPER
    move.w  #0x00f0, 0x00dff180

    /*
     * Take a copy of the arrival state before anything can disturb it.
     * a1 doubles as the buffer pointer, which costs us its original
     * value -- rescued through d0, whose own value is already stored by
     * the time d0 is reused as the temporary.
     */
    lea     buf(%pc), %a1
    move.l  %d0, (%a1)+
    move.l  %d1, (%a1)+
    move.l  %d2, (%a1)+
    move.l  %d3, (%a1)+
    move.l  %d4, (%a1)+
    move.l  %d5, (%a1)+
    move.l  %d6, (%a1)+
    move.l  %d7, (%a1)+
    move.l  %a0, (%a1)+
    move.l  %a1, %d0
    move.l  %d0, (%a1)+
    move.l  %a2, (%a1)+
    move.l  %a3, (%a1)+
    move.l  %a4, (%a1)+
    move.l  %a5, (%a1)+
    move.l  %a6, (%a1)+
    move.l  %sp, %d0              /* nothing has been pushed yet         */
    move.l  %d0, (%a1)+
    moveq   #0, %d0
    move.w  %sr, %d0
    move.l  %d0, (%a1)+

    lea     msg(%pc), %a0
    bsr     puts

    /* The buffer's own address: this is where the ROM put the block. */
    lea     buf(%pc), %a0
    move.l  %a0, %d0
    bsr     phex8
    bsr     space

    /* SysBase at absolute address 4 -- the ROM's, if it left one. */
    move.l  #4, %a0
    move.l  (%a0), %d0
    bsr     phex8
    bsr     nl

    lea     buf(%pc), %a0
    moveq   #NREGS-1, %d5
1:  move.l  (%a0)+, %d0
    bsr     phex8
    bsr     space
    dbf     %d5, 1b
    bsr     nl

2:  move.w  #0x000f, 0x00dff180
    bra.s   2b

/* ------------------------------------------------------------------ */

/* puts: NUL-terminated string at a0. */
puts:
    move.b  (%a0)+, %d2
    beq.s   1f
    bsr     putc
    bra.s   puts
1:  rts

/* putc: byte in d2.  Paced by polling SERDATR -- the emulator holds a
   byte queued until the shift register drains, and any write inside that
   window overwrites it, so unpaced output loses most of its bytes.  The
   wait is bounded so a transmitter that is not there cannot hang boot. */
putc:
    move.l  #0x00080000, %d4
1:  move.w  SERDATR, %d3
    andi.w  #STATMSK, %d3
    cmpi.w  #STATMSK, %d3
    beq.s   2f
    subq.l  #1, %d4
    bne.s   1b
2:  move.b  %d2, SERDAT
    rts

/* phex8 / phex4: value in d0 as eight / four hex digits.  Leaves d5 alone
   so the dump loop's counter survives, and a0 alone so it can walk. */
phex8:
    moveq   #7, %d1
    bra.s   phex
phex4:
    moveq   #3, %d1
phex:
1:  rol.l   #4, %d0
    move.b  %d0, %d2
    andi.b  #15, %d2
    cmpi.b  #10, %d2
    bhs.s   2f
    addi.b  #'0', %d2
    bra.s   3f
2:  addi.b  #'A'-10, %d2
3:  bsr     putc
    dbf     %d1, 1b
    rts

space:
    move.b  #' ', %d2
    bra     putc

nl:
    move.b  #10, %d2
    bra     putc

msg:
    .asciz  "NBBOOT\n"

    .balign 4
buf:
    .space  NREGS*4
