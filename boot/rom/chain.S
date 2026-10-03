/*
 * Chainload entry point, and the takeover that follows it.
 *
 * AmigaDOS reaches this by jumping to the first byte of the first hunk of
 * a Hunk executable run from S:Startup-sequence, after a genuine AmigaOS
 * 3.2.3 ROM has already brought the machine up.  That is a very different
 * arrival than reset: the chipset is programmed, the OS owns the vectors
 * and the exception stacks, and the CPU is already in whatever mode the
 * task was created in.
 *
 * The stage runs in two halves.  The first reports what it found and
 * decides whether the machine may be taken at all.  A hunk executable is
 * loaded wherever AllocMem() had room for it, and NeoBench pins a
 * handful of addresses into place at run time -- the frame buffer, the
 * arena, Paula's sound buffer, the stack -- so an image that landed in
 * front of one of them is handed straight back with `rts`, which is how
 * a hunk executable returns to the Shell, instead of being written over
 * by the desktop it would have booted.  The probe has a hazard of its
 * own: it writes a marker at the last two longs of every megabyte it
 * counts, so the image may not straddle a megabyte either.  Both are
 * asked of the image as [chain_entry, _end), which is the whole of the
 * hunk and nothing else.
 *
 * The second half is the takeover, and it assumes supervisor state.  A
 * user-mode arrival gets there the only way a user-mode arrival can:
 * vector 32 is pointed at the takeover and TRAP #0 is raised, which puts
 * the CPU into supervisor state for the handler.  The handler never
 * executes rte -- AmigaOS's frame is left where it was pushed, on
 * AmigaOS's own stack, and the machine is NeoBench's from the first
 * instruction.  Establishing the arrival mode before anything privileged
 * is attempted is the whole point of the first half: writing SR from
 * user mode raises a privilege violation that would take the rest of the
 * boot down with it.
 *
 * Diagnostics go to the serial port rather than the console.  The
 * Workbench console is about to disappear, and FS-UAE captures SERDAT
 * into serial_file no matter what the display is doing.
 */
    .section .text.chain_entry, "ax"
    .global chain_entry
    .extern bootvec_base
    .extern rom_main

    .equ    SERDAT,       0x00dff030
    .equ    SERDATR,      0x00dff018
    .equ    SERPER,       0x00dff032
    .equ    SERSTAT_MASK, 0x3000      /* TBE|TSRE, NDK hardware/custom.i */
    .equ    VEC_TRAP0,    0x00000080  /* vector 32, TRAP #0               */

chain_entry:
    /*
     * Arm the transmitter before anything else.  SERPER is not reset to a
     * usable value and the emulator derives its framing from it: left
     * alone it sits at period 16 (256000 baud), which FS-UAE cannot frame,
     * so every byte written before this point is discarded rather than
     * captured.  Without this line the whole diagnostic channel is silent.
     */
    move.w  #371, SERPER
    move.w  #'*', SERDAT           /* marker: reached the entry point     */

    movem.l %d0-%d7/%a0-%a6, -(%sp)

    /*
     * Work out which mode we arrived in without touching SR directly.
     *
     * MOVE from SR is privileged on 68010 and later -- only the 68000
     * reads it from user mode -- and AmigaDOS enters a program in user
     * mode, so the obvious read raises a privilege violation, exec takes
     * the fault and the Shell reports a failed program before a single
     * diagnostic byte has reached the wire.  That is what happened when
     * this stage asks %sr outright: the capture stopped dead after the
     * entry marker and AmigaDOS raised "Program failed".
     *
     * Ask from inside a trap instead.  TRAP #0 is dispatched through the
     * vector table, which is ordinary chip ram at address 0 (AmigaOS runs
     * with VBR clear and no MMU), so a single write buys us a handler that
     * runs in supervisor state for the length of one instruction; the CPU
     * owns the frame layout, so rte needs no knowledge of it.  The answer
     * is the stack the arrival was standing on: SP taken here, compared
     * against USP there, and the two coincide only for a user-mode arrival.
     *
     * d6 = 1 when we arrived in user mode, 0 when we arrived in
     * supervisor state.
     */
    move.l  %sp, %d5               /* the stack the arrival is on      */
    moveq   #0, %d6                /* presume supervisor               */
    movea.l #VEC_TRAP0, %a0
    move.l  (%a0), %d1             /* the OS's own handler, kept       */
    lea     sr_probe(%pc), %a1
    move.l  %a1, (%a0)
    trap    #0
    move.l  %d1, (%a0)             /* handed back before we go further  */

    lea     msg_head(%pc), %a0
    bsr     puts

    tst.l   %d6
    bne.s   1f                     /* user mode: nothing privileged now */

    /*
     * Supervisor.  Prove the privilege is really ours by raising the mask
     * to 7 and then putting the arrival SR straight back -- if that pair
     * runs, every later privileged instruction in the takeover will too.
     * The read is only safe here: this is the mode we are in.
     */
    move.w  %sr, %d7                 /* arrival SR: bit 13 is the S bit   */
    moveq   #'S', %d0
    bsr     putc
    move.w  #0x2700, %sr
    move.w  %d7, %sr
    lea     msg_sup(%pc), %a0
    bra.s   2f

1:  moveq   #'U', %d0
    bsr     putc
    lea     msg_usr(%pc), %a0

2:  bsr     puts

    /*
     * Where the hunk landed.  AllocMem() answers that and nothing else
     * does, and it is the whole of the question below: two hundred and
     * fifty kilobytes of NeoBench sitting in somebody else's frame buffer
     * is the one outcome that cannot be repaired from here.
     */
    lea     msg_at(%pc), %a0
    bsr     puts
    lea     chain_entry(%pc), %a0
    move.l  %a0, %d0
    bsr     putx8
    lea     msg_nl(%pc), %a0
    bsr     puts

    bsr     chain_safe
    tst.l   %d1
    bne.s   9f

    /*
     * Take it.  Supervisor already: straight in.  Otherwise point vector
     * 32 at the same code and trap into it, which is how a user-mode
     * arrival reaches supervisor state at all.  Neither path comes back.
     */
    tst.l   %d6
    beq.s   3f
    movea.l #VEC_TRAP0, %a0
    lea     takeover(%pc), %a1
    move.l  %a1, (%a0)
    trap    #0
    bra.s   9f
3:  bsr     takeover

9:  lea     msg_back(%pc), %a0
    bsr     puts
    move.b  #'>', %d0             /* paced like the rest, or it lands on
                                      top of the byte still shifting out */
    bsr     putc
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    /*
     * RunCommand() hands the program's exit code back to the Shell in d0,
     * and the arrival value is whatever the ROM happened to leave there.
     * Returning it unchanged reads as a failure -- AmigaDOS pops a
     * "Program failed" requester even though every diagnostic above
     * succeeded -- so hand back RETURN_OK.
     */
    moveq   #0, %d0
    rts

/*
 * chain_safe: may the machine be taken?
 *
 * d1 answers 0 for yes and 1 for no, asked of the image as
 * [chain_entry, _end).  Two independent hazards decide it.
 *
 * NeoBench fixes its working addresses into place at run time and writes
 * them without asking who was there first: the planar frame buffer and
 * the RGB565 back buffer at $010000..$110000, the arena at $110000, the
 * sound buffer at $150000..$1C0000, and the stack from $1E0000 to the
 * top of chip ram.  $110000..$150000 is the one gap in chip ram that is
 * clear -- it holds the reset build's BSS, and there is no reset build
 * here -- and fast ram above $200000 is clear by construction.
 *
 * mem_runs() has a hazard of its own.  It writes a marker at the last
 * two longs of every megabyte it counts, low or high, so an image that
 * reaches into those eight bytes is an image the probe edits.  Keeping
 * the image inside one megabyte and stopping it short of the marker is
 * the whole test: the first rejects a straddle, the second rejects an
 * image that ends hard against its own megabyte's tail.
 */
chain_safe:
    lea     chain_entry(%pc), %a0
    lea     _end, %a1
    moveq   #0, %d1

    move.l  %a0, %d0
    move.l  %a1, %d2
    eor.l   %d0, %d2
    cmpi.l  #0x00100000, %d2
    bhs.s   8f
    move.l  %a1, %d2
    andi.l  #0x000fffff, %d2
    cmpi.l  #0x000ffff8, %d2
    bhi.s   8f

    cmpi.l  #0x00110000, %d0
    bhs.s   1f
    move.l  %a1, %d2
    cmpi.l  #0x00010000, %d2
    bhi.s   8f
1:  cmpi.l  #0x001c0000, %d0
    bhs.s   2f
    move.l  %a1, %d2
    cmpi.l  #0x00150000, %d2
    bhi.s   8f
2:  cmpi.l  #0x00200000, %d0
    bhs.s   3f
    move.l  %a1, %d2
    cmpi.l  #0x001e0000, %d2
    bhi.s   8f
3:  rts
8:  moveq   #1, %d1
    rts

/*
 * takeover: the machine, from the state AmigaDOS left it in.
 *
 * Entered in supervisor state with interrupts live and AmigaOS's table
 * still at address 0, so the mask goes up in the first instruction and
 * nothing privileged is attempted before it has.  From there it is the
 * same sequence _start runs after a reset, with two differences: the
 * vector table travels inside the hunk rather than out of a ROM at
 * $F80000 -- that address holds the Kickstart which is still in the
 * socket -- and the stack has to be taken deliberately, because the
 * reset SSP in the table is only loaded by the CPU on a reset.
 *
 * Nothing here returns.  The last instruction is the same jsr rom_main
 * the reset path uses, and the frame pushed by the trap that got a
 * user-mode arrival here is abandoned on AmigaOS's stack, where nothing
 * will ever read it again.
 */
takeover:
    move.w  #0x2700, %sr            /* supervisor, everything masked     */

    /*
     * What the OS left in the cache control register, on the wire before
     * anything depends on it.  The reset path reaches this point with the
     * register at its power-on value because nothing has touched it, and
     * the difference matters: a write-back data cache that the chainload
     * inherits would hold the frame buffer where Paula's DMA cannot see
     * it, while the ROM build's display is written straight to memory.
     * MOVEC of CACR is control register 2 on every CPU this boots on --
     * AGA starts at the 68020, and the 68020 is where CACR starts too.
     */
    moveq   #0, %d0
    .word   0x4E7A, 0x0002          /* movec cacr, d0                    */
    lea     msg_cacr(%pc), %a0
    bsr     puts
    bsr     putx8
    lea     msg_nl(%pc), %a0
    bsr     puts

    /*
     * Drop the ROM boot overlay (CIA A PRA bit 0), exactly as _start
     * does.  Kickstart dropped it in ROMInit and AmigaOS has not
     * asserted it since, so this reads the port and writes the same byte
     * back -- but a chainload that did not would be relying on that.
     */
    move.b  0x00BFE001, %d0
    andi.b  #0xfe, %d0
    move.b  %d0, 0x00BFE001

    /*
     * Own the vectors.  Ours rides inside the hunk and has already had
     * the loader's relocation applied to it, so the pointers in it reach
     * the code that is running, wherever AllocMem() put that code.
     */
    lea     bootvec_base, %a0
    lea     0x00000000, %a1
    move.w  #0x00FF, %d0            /* dbra: 256 longs = 1024 bytes      */
1:
    move.l  (%a0)+, (%a1)+
    dbra    %d0, 1b

    /* Take the stack the reset SSP names: top of 2 MB of chip ram. */
    lea     0x001FFFE0, %sp

    /* Zero BSS, which lives in the hunk here rather than at $110000. */
    lea     _bss_start, %a0
    lea     _bss_end, %a1
2:
    cmpa.l  %a1, %a0
    bhs.s   3f
    clr.l   (%a0)+
    bra.s   2b
3:
    jsr     rom_main

4:
    bra.s   4b

/* puts: print the NUL-terminated string at a0. */
puts:
    movem.l %a0, -(%sp)
1:  move.b  (%a0)+, %d0
    beq.s   2f
    bsr.s   putc
    bra.s   1b
2:  movem.l (%sp)+, %a0
    rts

/* putc: print the byte in d0.
 *
 * Paced by polling SERDATR rather than by a delay: the emulator keeps a
 * byte queued until the shift register drains (about 14 scanlines at 9600
 * baud) and any write inside that window overwrites it, so unpaced output
 * loses most of its bytes.  The wait is bounded, so a transmitter that is
 * simply not there can never hang the boot.
 *
 * The store is a word, and the char is masked into the low byte first.
 * A byte write to an even custom register is driven onto D15-D8, which
 * the emulator shifts into the high lane, leaving the eight bits Paula
 * actually transmits at zero -- on a 68040 or later the write bug is
 * configured unconditionally, so `move.b` to SERDAT silently emits NULs.
 */
putc:
    move.l  #0x00080000, %d1
1:  move.w  SERDATR, %d2
    andi.w  #SERSTAT_MASK, %d2
    cmpi.w  #SERSTAT_MASK, %d2
    beq.s   2f
    subq.l  #1, %d1
    bne.s   1b
2:  andi.w  #0x00ff, %d0
    move.w  %d0, SERDAT
    rts

/* putx8: print the long in d0 as eight hex digits, most significant
 * first.  putc leaves only d0's low byte alone, so the value has to live
 * somewhere putc cannot reach while it is being cut into nibbles.
 */
putx8:
    move.l  %d0, %d3
    moveq   #7, %d4
1:  rol.l   #4, %d3
    move.l  %d3, %d0
    andi.l  #0x0000000f, %d0
    addi.l  #'0', %d0
    cmpi.l  #'9', %d0
    ble.s   2f
    addi.l  #7, %d0                 /* 'A' is seven past '9'            */
2:  bsr     putc
    dbra    %d4, 1b
    rts

/*
 * sr_probe: the body of vector 32 for the length of one TRAP #0.
 *
 * Runs in supervisor state whether or not we arrived there, so USP is
 * readable and rte is legal.  Registers are not preserved by the CPU
 * across a trap, but everything the arrival owned is already on the stack
 * above, so only d6 is written -- and only to record the answer.
 */
sr_probe:
    move.l  %usp, %a1
    cmp.l   %a1, %d5               /* d5 was SP at the moment of the trap */
    bne.s   8f                     /* not the user stack: we were here    */
    moveq   #1, %d6
8:  rte

msg_head:
    .asciz  "\nNBCHAIN mode="
msg_sup:
    .asciz  " mask=7 ok"
msg_usr:
    .asciz  " (user mode)"
msg_at:
    .asciz  " at="
msg_nl:
    .asciz  "\n"
msg_back:
    .asciz  "NBCHAIN image in the way, returning to AmigaOS\n"
msg_cacr:
    .asciz  "NBCHAIN cacr="
    .balign 2
