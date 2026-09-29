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
    .equ    SERDATR,      0x00dff038
    .equ    SERPER,       0x00dff032
    .equ    SERSTAT_MASK, 0x3000      /* TBE|TSRE, NDK hardware/custom.i */

chain_entry:
    /*
     * Arm the transmitter before anything else.  SERPER is not reset to a
     * usable value and the emulator derives its framing from it: left
     * alone it sits at period 16 (256000 baud), which FS-UAE cannot frame,
     * so every byte written before this point is discarded rather than
     * captured.  Without this line the whole diagnostic channel is silent.
     */
    move.w  #371, SERPER
    move.b  #'*', SERDAT           /* marker: reached the entry point     */

    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.w  %sr, %d7                 /* arrival SR: bit 13 is the S bit   */

    lea     msg_head(%pc), %a0
    bsr.s   puts

    btst    #13, %d7
    beq.s   1f

    /*
     * Supervisor.  Prove the privilege is really ours by raising the mask
     * to 7 and then putting the arrival SR straight back -- if that pair
     * runs, every later privileged instruction in the takeover will too.
     */
    moveq   #'S', %d0
    bsr.s   putc
    move.w  #0x2700, %sr
    move.w  %d7, %sr
    lea     msg_sup(%pc), %a0
    bra.s   2f

1:  moveq   #'U', %d0
    bsr.s   putc
    lea     msg_usr(%pc), %a0

2:  bsr.s   puts
    move.b  #'>', %d0             /* paced like the rest, or it lands on
                                      top of the byte still shifting out */
    bsr.s   putc
    movem.l (%sp)+, %d0-%d7/%a0-%a6
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
 * simply not there can never hang the boot. */
putc:
    move.l  #0x00080000, %d1
1:  move.w  SERDATR, %d2
    andi.w  #SERSTAT_MASK, %d2
    cmpi.w  #SERSTAT_MASK, %d2
    beq.s   2f
    subq.l  #1, %d1
    bne.s   1b
2:  move.b  %d0, SERDAT
    rts

msg_head:
    .asciz  "\nNBCHAIN mode="
msg_sup:
    .asciz  " mask=7 ok\n"
msg_usr:
    .asciz  " (user mode, takeover deferred)\n"
    .balign 2
