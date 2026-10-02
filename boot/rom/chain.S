/*
 * Chainload entry point.
 *
 * AmigaDOS reaches this by jumping to the first byte of the first hunk of
 * a Hunk executable run from S:Startup-sequence, after a genuine AmigaOS
 * 3.2.3 ROM has already brought the machine up.  That is a very different
 * arrival than reset: the chipset is programmed, the OS owns the vectors
 * and the exception stacks, and the CPU is already in whatever mode the
 * task was created in.
 *
 * This first stage only reports what it found and hands control straight
 * back with `rts`, which is how a hunk executable returns to the Shell.
 * Establishing the arrival mode before anything privileged is attempted is
 * the whole point of the stage: the takeover that follows assumes
 * supervisor state, and writing SR from user mode raises a privilege
 * violation that would take the rest of the boot down with it.
 *
 * Diagnostics go to the serial port rather than the console.  The
 * Workbench console is about to disappear, and FS-UAE captures SERDAT
 * into serial_file no matter what the display is doing.
 */
    .section .text.chain_entry, "ax"
    .global chain_entry

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
     * this stage asked %sr outright: the capture stopped dead after the
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
    .asciz  " mask=7 ok\n"
msg_usr:
    .asciz  " (user mode, takeover deferred)\n"
    .balign 2
