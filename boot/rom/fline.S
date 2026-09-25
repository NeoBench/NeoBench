/*
 * Hardware feature probes for the boot self-test.
 *
 * Each probe executes one instruction that exists only when the CPU feature
 * under test is present.  When it is absent the CPU raises an exception;
 * nb_fline_handler rewrites the saved PC to the recovery label, so a
 * negative result is just a caught trap -- never a fault stub halt.
 *
 * Three vectors are shimmed for the duration of a probe, because the
 * exception an absent instruction raises differs by generation:
 *
 *   11 -- line-F, what the 68000..68040 raise on an unassigned opcode
 *    4 -- illegal instruction, how the 68060 rejects removed ones when
 *         WinUAE's "int_no_unimplemented" mode is off
 *   61 -- the 68060's Unimplemented Instruction exception when it is on
 *
 * The handler only touches a frame while nb_probe_pc is non-zero (it is
 * cleared the moment the shim is installed and removed), and every
 * exception that can arrive in that window belongs to the probe.  Real
 * 68060 hardware raises vector 61 for the instructions the 68040 had and
 * the 68060 dropped, so the same catch covers silicon.
 */
    .text
    .global nb_fline_handler
    .global nb_probe_fpu
    .global nb_probe_mmu
    .global nb_probe_move16
    .global nb_probe_pcr
    .global nb_fpu_enable_060
    .extern nb_probe_pc
    .extern nb_probe_recover

/*
 * Temporary handler for vectors 4/11/61.  After the two-register save the
 * original frame sits at 0(%sp): SR at 0, saved PC as a long at 2,
 * format/vector at 6 -- so the PC is at 10(%sp) here.
 */
nb_fline_handler:
    movem.l %d0/%a0, -(%sp)
    move.l  nb_probe_pc, %d0
    beq.s   1f                      /* 0: outside a probe: leave frame   */
    move.l  nb_probe_recover, %d0
    move.l  %d0, 10(%sp)            /* resume at the recovery label      */
1:
    movem.l (%sp)+, %d0/%a0
    rte

    /* Install the trap shim around a probe sequence. */
    .macro  VEC_ON
    movem.l %d2/%d3/%d4/%a2, -(%sp)
    clr.l   nb_probe_pc             /* window opens: traps are ignored   */
    move.l  0x0000002C, %d2         /* vector 11: line-F emulator        */
    move.l  0x00000010, %d3         /* vector  4: illegal instruction     */
    move.l  0x000000F4, %d4         /* vector 61: 68060 unimplemented     */
    lea     nb_fline_handler, %a2
    move.l  %a2, 0x0000002C
    move.l  %a2, 0x00000010
    move.l  %a2, 0x000000F4
    .endm

    .macro  VEC_OFF
    move.l  %d4, 0x000000F4         /* put the original handlers back    */
    move.l  %d3, 0x00000010
    move.l  %d2, 0x0000002C
    clr.l   nb_probe_pc             /* window closes                     */
    movem.l (%sp)+, %d2/%d3/%d4/%a2
    .endm

    /*
     * int nb_probe_fpu(void) -- 1 when the CPU executes FNOP.
     *
     * FNOP is the one FPU instruction with no operands and no side effects,
     * identical on 68881, 68040 and 68060 (F280 0000), and always four
     * bytes, so the recovery label works without any per-CPU handling.
     */
    .global nb_probe_fpu
nb_probe_fpu:
    VEC_ON
    moveq   #0, %d0
    lea     1f, %a2
    move.l  %a2, nb_probe_pc
    lea     2f, %a2
    move.l  %a2, nb_probe_recover
1:
    .word   0xF280, 0x0000          /* fnop */
    moveq   #1, %d0
2:
    VEC_OFF
    rts

    /*
     * int nb_probe_mmu(void) -- 1 when an ATC flush instruction runs.
     *
     * PFLUSHA exists on every m68k MMU, but the opcode AND the instruction
     * length differ between generations: F000 2400 (four bytes) on the
     * 68030, F518 (two bytes) on the 68040/68060.  Try the 68030 form
     * first; only if it traps, try the 68040 form.  On a 68020 both trap
     * (coprocessor ID 0/2 are unconfigured) and the answer is a clean 0.
     */
    .global nb_probe_mmu
nb_probe_mmu:
    VEC_ON
    moveq   #0, %d0
    lea     1f, %a2
    move.l  %a2, nb_probe_pc
    lea     2f, %a2
    move.l  %a2, nb_probe_recover
1:
    .word   0xF000, 0x2400          /* pflusha, 68030 encoding */
    moveq   #1, %d0
2:
    tst.l   %d0
    bne.s   6f                      /* 68030 form ran: MMU present */
    lea     4f, %a2
    move.l  %a2, nb_probe_pc
    lea     5f, %a2
    move.l  %a2, nb_probe_recover
4:
    .word   0xF518                  /* pflusha, 68040/68060 encoding */
    moveq   #1, %d0
5:
6:
    VEC_OFF
    rts

    /*
     * int nb_probe_move16(void) -- 1 when MOVE16 runs (68040/68060).
     *
     * F600 <addr.l>: move16 (a0)+,$addr -- the one 68040 instruction that
     * moves sixteen bytes between two RAM addresses in a single opcode, so
     * both endpoints are our own probe buffer.  Six bytes total, and it
     * traps as line-F on every CPU without the quad-move unit (68030 and
     * below).
     */
    .global nb_probe_move16
nb_probe_move16:
    VEC_ON
    moveq   #0, %d0
    lea     nb_probe_buf, %a0
    lea     1f, %a2
    move.l  %a2, nb_probe_pc
    lea     2f, %a2
    move.l  %a2, nb_probe_recover
1:
    .word   0xF600                  /* move16 (a0)+,$addr.l            */
    .long   nb_probe_buf
    moveq   #1, %d0
2:
    VEC_OFF
    rts

    /*
     * int nb_probe_pcr(void) -- 1 when MOVEC PCR runs (68060 only).
     *
     * The PCR (Processor Configuration Register, control register $808)
     * is what the 68060 added over the 68040, so movec of it executes on
     * a 68060 and raises an illegal instruction (vector 4) on a 68040 --
     * on real silicon as well as in the emulator, where the answer does
     * not depend on any accuracy preference.  Reading it has no side
     * effects; the value is discarded.
     */
    .global nb_probe_pcr
nb_probe_pcr:
    VEC_ON
    moveq   #0, %d0
    lea     1f, %a2
    move.l  %a2, nb_probe_pc
    lea     2f, %a2
    move.l  %a2, nb_probe_recover
1:
    .word   0x4E7A, 0x0808          /* movec pcr,d0 */
    moveq   #1, %d0
2:
    VEC_OFF
    rts

    /*
     * void nb_fpu_enable_060(void) -- clear the PCR FPU-disable bit.
     *
     * A 68060 powers up with its FPU switched off (accelerator ROMs leave
     * it that way until the OS identifies the CPU; WinUAE models exactly
     * that whenever a kickstart ROM is loaded -- which is how we boot).
     * NeoBench requires a working FPU on its only supported CPU, so the
     * boot enables it before probing.  PCR only allows bits 0, 1 and 6 to
     * be written, clearing an already-clear bit changes nothing, so this
     * is also safe on silicon where something got there first.  Only ever
     * called after nb_probe_cpu() answered 68060 -- on anything else the
     * movec would be an illegal instruction.
     */
    .global nb_fpu_enable_060
nb_fpu_enable_060:
    .word   0x4E7A, 0x0808          /* movec pcr,d0 */
    andi.l  #0xFFFFFFFD, %d0        /* PCR bit 1: FPU disable */
    .word   0x4E7B, 0x0808          /* movec d0,pcr */
    rts

    /*
     * Scratch target for MOVE16.  Sixteen-byte alignment because MOVE16
     * wants both ends of the transfer aligned.
     */
    .bss
    .balign 16
    .global nb_probe_buf
nb_probe_buf:
    .space 16
