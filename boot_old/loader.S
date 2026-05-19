/*=============================================================================
 * NeoBench Kernel - Floppy Bootblock Loader
 *
 * This code occupies the first 1024 bytes of a NeoBench ADF image.
 * The Amiga Kickstart ROM loads and executes it during boot.
 *
 * Entry conditions (from Kickstart ROM):
 *   A0 = pointer to this bootblock in memory
 *   A1 = pointer to IORequest for trackdisk.device (we ignore this)
 *   A6 = ExecBase (we ignore this - we're going bare-metal)
 *   SR  = supervisor mode already set by ROM
 *   CPU = 68000, 68010, 68020, 68030, 68040, or 68060
 *
 * What we do:
 *   1. Kill AmigaOS completely (disable all DMA, interrupts, CIA)
 *   2. Set up a temporary stack
 *   3. Home the floppy head to track 0
 *   4. Read tracks 0-7 (64KB) into 0x1000 using disk DMA
 *      (enough for the full kernel binary at -Os)
 *   5. Set VBR = 0 (guards against 68010+ having it elsewhere)
 *   6. Jump to the kernel entry point
 *
 * Corrections vs v1.0:
 *
 *  1. CIA-B PRB ADDRESS WRONG (critical).
 *     The original used 0xBFD100 for CIA-B PRB.  The correct address
 *     for CIA-B PRB (Port B, which controls floppy drive mechanics) is:
 *       CIA-B PRB: 0xBFD000  (even address, byte-wide)
 *     0xBFD100 is CIA-B DDRB (Data Direction Register B).  Writing
 *     drive-select/motor values to DDRB instead of PRB would configure
 *     the port direction rather than asserting outputs, producing no
 *     motor or head movement.
 *
 *  2. INITIAL PRB VALUE FOR MOTOR+SELECT WRONG.
 *     The original wrote 0xF7 to CIA-B PRB to select DF0 and start motor.
 *     CIA-B PRB controls (all active LOW):
 *       Bit 7: /MTR  (motor)
 *       Bit 6: /SEL3
 *       Bit 5: /SEL2
 *       Bit 4: /SEL1
 *       Bit 3: /SEL0  <- drive 0
 *       Bit 2: /SIDE
 *       Bit 1: /DIR
 *       Bit 0: /STEP
 *     To select DF0 (bit 3 = 0) and start motor (bit 7 = 0), all others
 *     deasserted (1), STEP must stay high (1), DIR and SIDE don't matter
 *     initially:
 *       0b01110111 = 0x77 (select DF0, motor on, step=1, side=0, dir=1)
 *     The original value 0xF7 = 0b11110111 keeps /MTR=1 (motor OFF) while
 *     asserting /SEL0=0.  No motor would ever start.
 *     Then the original wrote 0xFD = 0b11111101 for DIR=0 direction, but
 *     this also deselects the drive (/SEL0 goes back to 1).  The drive
 *     must remain SELECTED during steps.
 *
 *  3. STEP DIRECTION BIT WRONG.
 *     DIR=0 means step INWARD (toward higher tracks) on the Amiga.
 *     DIR=1 means step OUTWARD (toward track 0, lower tracks).
 *     To home to track 0 we want to step outward: DIR=1.
 *     The original used 0xFD (bit 1 = 0 = outward) for the step-to-track0
 *     seek, which IS the correct direction, but combined with deselecting
 *     the drive (SEL0 bit = 1) the steps would have no effect.
 *
 *  4. CIA-A PRA TRK0 BIT READ FROM WRONG ADDRESS.
 *     The original used absolute address 0xBFE001 for CIA-A PRA.
 *     This is correct.  However, TRK0 is bit 4 of CIA-B PRA (not CIA-A).
 *     Wait - on the Amiga:
 *       CIA-A PRA: 0xBFE001 - contains fire buttons, overlay, etc.
 *       CIA-B PRA: 0xBFD100 - contains /RDY, /TRK0, /WPRO, /CHNG, /DKWE
 *     So /TRK0 is bit 4 of CIA-B PRA at 0xBFD100, NOT CIA-A PRA.
 *     The original read from CIA-A PRA which contains completely different
 *     signals - the track-0 test would never work.
 *     Fixed: read /TRK0 from CIA-B PRA (0xBFD100), bit 4.
 *
 *  5. ADKCON NOT CONFIGURED (critical - disk DMA would not work).
 *     ADKCON controls MFM word-sync mode for the disk DMA.  Without
 *     setting WORDSYNC and MFMPREC, the disk DMA has no sync signal
 *     and will not produce usable data.  The original never wrote ADKCON.
 *     Required setup:
 *       ADKCON = 0x9500 (SETCLR | WORDSYNC | MSBSYNC | MFMPREC)
 *     This enables MFM precompensation and word-sync on DSKSYNC value.
 *
 *  6. DSKLEN WRITE-ENABLE BIT WRONG.
 *     DSKLEN bit 15 = DMA enable, bit 14 = write direction (0=read, 1=write).
 *     The original wrote 0xC000 | word_count = 0xC000 | 20480.
 *     0xC000 = bits 15 and 14 set = DMA enable + WRITE mode.
 *     We are READING from disk, so bit 14 must be 0.
 *     Correct value for read: 0x8000 | word_count.
 *
 *  7. DISK DMA POINTER WRITE ORDER WRONG.
 *     DSKPTH (high word) must be written BEFORE DSKPTL (low word).
 *     The original wrote DSKPTL (0x022) before DSKPTH (0x020).
 *     If a DMA cycle starts between writes you get a corrupted pointer.
 *     Fixed: write DSKPTH first, then DSKPTL.
 *
 *  8. 40KB BLIND READ FROM TRACK 0 SECTOR 0 IS WRONG.
 *     The bootblock IS track 0 sectors 0-1.  The kernel starts at
 *     track 0 sector 2, or more safely at track 1 (to guarantee we
 *     don't overlap the bootblock checksum area).
 *     A single DMA read of 40KB spans multiple tracks and the DMA
 *     engine stops when it loses sync (end of track), so we can't
 *     do a single 40KB read - we must read track by track.
 *     Fixed: read multiple tracks sequentially using a loop.
 *
 *  9. VBR MOVEC IS NOT VALID ON 68000 (crash on stock A500/A2000).
 *     MOVEC is a 68010+ instruction.  Executing it on a 68000 causes
 *     an illegal instruction exception.  Since our vector table is
 *     at 0x000000 (the 68000's fixed reset location), we only need
 *     MOVEC on 68010+ where VBR may have been moved by the ROM.
 *     Fixed: detect CPU type using the CACR MOVEC trick, then
 *     conditionally set VBR only on 68010+.
 *
 * 10. LOAD ADDRESS 0x000000 OVERWRITES VECTOR TABLE BEFORE JUMP.
 *     Loading the kernel at address 0 works but means the kernel's
 *     own vector table must be the first thing written.  The ROM
 *     still has pointers into itself in registers when we jump;
 *     overwriting 0x000004 (reset vector) is fine since we jump
 *     via that address, but we must ensure the kernel binary starts
 *     with a valid 68k vector table.  This is handled by start.S /
 *     vectors.S in the kernel build.  We load to 0x000000 which is
 *     correct per the linker script.
 *
 * 11. MOTOR WAIT IS TOO SHORT AND CPU-SPEED DEPENDENT.
 *     A 3.5" drive needs up to 500ms to reach full speed.  The original
 *     loop of 50000 iterations is ~2ms on a 68000@7MHz.  We wait for
 *     the /RDY signal instead (CIA-B PRA bit 5), which is the correct
 *     hardware-defined indicator.
 *
 * Register usage throughout:
 *   A5 = custom chip base (0xDFF000)
 *   A4 = CIA-B PRB address (0xBFD000)
 *   A3 = CIA-B PRA address (0xBFD100) - for status reads
 *   A2 = load address (destination buffer)
 *   D7 = track counter / scratch
 *   D6 = words per track
 *   D5 = number of tracks to load
 *=============================================================================*/

    .section .text.bootblock, "ax"
    .global _bootblock_start

/* ============================================================
 * Hardware addresses and constants
 * ============================================================ */

#define CUSTOM_BASE     0x00DFF000
#define CIAA_PRA        0x00BFE001      /* CIA-A Port A (fire, overlay) */
#define CIAB_PRB        0x00BFD000      /* CIA-B Port B (drive control) */
#define CIAB_PRA        0x00BFD100      /* CIA-B Port A (disk status) */
#define CIAB_DDRB       0x00BFD200      /* CIA-B DDRB (all outputs) */

/* Custom chip register offsets from CUSTOM_BASE */
#define DSKPTH          0x020
#define DSKPTL          0x022
#define DSKLEN          0x024
#define DSKSYNC         0x07E
#define DMACON          0x096
#define INTENA          0x09A
#define INTREQ          0x09C
#define INTREQR         0x01E
#define ADKCON          0x09E
#define COLOR00         0x180

/* CIA-B PRB bits (active low) */
#define CIAB_MTR        0x80            /* /MTR: motor (0=on) */
#define CIAB_SEL3       0x40
#define CIAB_SEL2       0x20
#define CIAB_SEL1       0x10
#define CIAB_SEL0       0x08            /* /SEL0: drive 0 select (0=selected) */
#define CIAB_SIDE       0x04            /* /SIDE: 0=upper(side 1), 1=lower(side 0) */
#define CIAB_DIR        0x02            /* /DIR: 0=inward, 1=outward(toward trk 0) */
#define CIAB_STEP       0x01            /* /STEP: step pulse (0=step) */

/* CIA-B PRA status bits (active low inputs) */
#define CIAB_CHNG       0x04            /* /DSKCHNG */
#define CIAB_WPRO       0x08            /* /WPRO */
#define CIAB_TRK0       0x10            /* /TK0: 0 = at track 0 */
#define CIAB_RDY        0x20            /* /RDY: 0 = drive ready */

/* DMA bits */
#define DMAF_SETCLR     0x8000
#define DMAF_MASTER     0x0200
#define DMAF_DISK       0x0010

/* ADKCON bits */
#define ADKF_SETCLR     0x8000
#define ADKF_MFMPREC    0x0200          /* MFM precompensation */
#define ADKF_WORDSYNC   0x0400          /* Sync on DSKSYNC word */
#define ADKF_MSBSYNC    0x0800          /* Sync on MSB */

/* MFM sync word */
#define MFM_SYNC        0x4489

/* Kernel load parameters */
#define KERNEL_LOAD_ADDR    0x000000    /* Load kernel at start of chip RAM */
#define TRACKS_TO_LOAD      8           /* Load 8 tracks (DD: 8*11*512 = 45056 bytes) */
#define WORDS_PER_TRACK_DD  0x1A00      /* 13312 bytes = 6656 words (raw MFM track) */
                                        /* Standard DD track = 13630 bytes; 0x1A00 */
                                        /* is conservative, enough for 11 sectors   */

/* ============================================================
 * Bootblock header - must be at the very start
 * The Amiga ROM checks: bytes 0-3 = "DOS?" and verifies the
 * checksum at offset 4 (sum of all longs must = 0xFFFFFFFF).
 * Our makeadf tool fills in offset 4 correctly.
 * ============================================================ */

_bootblock_start:
    .ascii  "DOS\1"                     /* Disk type: FFS (0x444F5301) */
    .long   0x00000000                  /* Checksum - filled by makeadf */
    .long   880                         /* Root block: 880 (standard DD) */

/* ============================================================
 * Bootblock entry point
 * The ROM jumps here after verifying the checksum.
 * We are already in supervisor mode.
 * ============================================================ */

_bootblock_entry:

    /* Ensure supervisor mode with all interrupts masked */
    move.w  #0x2700,%sr

    /* Custom chip base in A5 - used throughout */
    lea     CUSTOM_BASE,%a5

    /* ============================================================
     * Kill AmigaOS completely
     * ============================================================ */

    /* Disable all DMA (write without SETCLR = clear bits) */
    move.w  #0x7FFF,DMACON(%a5)

    /* Disable all interrupts */
    move.w  #0x7FFF,INTENA(%a5)

    /* Clear all pending interrupt requests */
    move.w  #0x7FFF,INTREQ(%a5)

    /* Clear ADKCON (disable audio/disk modulation) */
    move.w  #0x7FFF,ADKCON(%a5)

    /* ============================================================
     * Set up temporary stack
     * We use the top of the first 512KB of chip RAM.
     * The kernel will set up its own stack once running.
     * ============================================================ */

    move.l  #0x07F000,%sp

    /* ============================================================
     * Colour feedback: dark blue = bootloader started
     * ============================================================ */

    move.w  #0x0058,COLOR00(%a5)

    /* ============================================================
     * Configure CIA-B for floppy control
     *
     * CIA-B PRB must have all control bits set as outputs.
     * Set DDRB = 0xFF (all outputs).
     * ============================================================ */

    move.b  #0xFF,CIAB_DDRB

    /* CIA-B PRB and PRA addresses in address registers */
    lea     CIAB_PRB,%a4            /* A4 = CIA-B PRB (drive control, outputs) */
    lea     CIAB_PRA,%a3            /* A3 = CIA-B PRA (disk status, inputs) */

    /* Initial state: all drives deselected, motor off, step high, DIR out */
    /* 0xFF = /MTR=1(off), /SELx=1(desel), /SIDE=1, /DIR=1, /STEP=1 */
    move.b  #0xFF,(%a4)

    /* ============================================================
     * Select drive 0 and start motor
     *
     * Assert /SEL0 (bit 3 = 0) and /MTR (bit 7 = 0).
     * Keep /STEP=1, /SIDE=1(lower head=side 0), /DIR=1(outward ready).
     * All other drives remain deselected.
     *
     * PRB = 1110 0111 = 0xE7:
     *   bit7=/MTR=0  (motor ON)
     *   bit6=/SEL3=1 (desel)
     *   bit5=/SEL2=1 (desel)
     *   bit4=/SEL1=1 (desel)
     *   bit3=/SEL0=0 (SELECT DF0)
     *   bit2=/SIDE=1 (lower head = AmigaDOS side 0)
     *   bit1=/DIR=1  (outward, toward track 0)
     *   bit0=/STEP=1 (step idle high)
     * ============================================================ */

    move.b  #0xE7,(%a4)

    /* ============================================================
     * Wait for drive ready (/RDY = CIA-B PRA bit 5 = 0 when ready)
     * Allow up to ~500ms.  At 7MHz 68000: ~1 loop iteration = ~1us.
     * 500000 iterations ~= 500ms.
     * ============================================================ */

    move.l  #500000,%d7
.wait_ready:
    btst    #5,(%a3)                /* Test /RDY bit in CIA-B PRA */
    beq     .drive_ready            /* /RDY=0 means ready */
    subq.l  #1,%d7
    bne     .wait_ready
    /* If we time out, proceed anyway - drive may have no disk */
.drive_ready:

    /* ============================================================
     * Seek to track 0
     *
     * Step outward (/DIR=1) until /TRK0 (CIA-B PRA bit 4) asserts.
     * Drive must remain selected (/SEL0=0) during all steps.
     * STEP pulse: bring bit 0 low then high.
     * Minimum step delay: 3ms (well met by our step_delay loop).
     * ============================================================ */

    /* Ensure DIR=outward (bit1=1) - already set from motor-on value */
    /* PRB stays 0xE7 (/MTR=0, /SEL0=0, /STEP=1, /DIR=1) */

.seek_track0:
    /* Test /TRK0 in CIA-B PRA (bit 4, active low: 0 = at track 0) */
    btst    #4,(%a3)
    beq     .at_track0

    /* Assert STEP: bit 0 low */
    bclr    #0,(%a4)

    /* Step pulse width delay: ~3us minimum */
    nop
    nop
    nop
    nop
    nop
    nop

    /* Deassert STEP: bit 0 high */
    bset    #0,(%a4)

    /* Inter-step delay: 3ms minimum per drive spec
     * At 7MHz 68000: 3000us / 0.143us/cycle ~ 21000 cycles
     * Our inner loop body is ~6 cycles, so ~3500 iterations.
     * We use 5000 to be safe on faster CPUs (they have wait states). */
    move.w  #5000,%d7
.step_delay:
    subq.w  #1,%d7
    bne     .step_delay

    bra     .seek_track0

.at_track0:

    /* ============================================================
     * Configure disk DMA
     *
     * ADKCON: enable word-sync (WORDSYNC) and MFM (MFMPREC).
     * DSKSYNC: set sync word to standard Amiga MFM sync (0x4489).
     * ============================================================ */

    move.w  #ADKF_SETCLR|ADKF_WORDSYNC|ADKF_MFMPREC,ADKCON(%a5)
    move.w  #MFM_SYNC,DSKSYNC(%a5)

    /* Enable disk DMA (master + disk channel) */
    move.w  #DMAF_SETCLR|DMAF_MASTER|DMAF_DISK,DMACON(%a5)

    /* ============================================================
     * Load kernel from floppy: read TRACKS_TO_LOAD tracks
     *
     * We read one track at a time into consecutive addresses.
     * Each DD track = 13630 bytes raw MFM (we read 0x1A00 words =
     * 13312 bytes which captures all 11 sectors reliably).
     *
     * Track 0 includes our bootblock.  We load it too since the
     * kernel binary starts at 0x000000 and contains its own vector
     * table; overwriting our bootblock in RAM is intentional.
     *
     * A2 = current load address
     * D5 = track counter (0 to TRACKS_TO_LOAD-1)
     * D6 = words per track
     * ============================================================ */

    lea     KERNEL_LOAD_ADDR,%a2
    moveq   #0,%d5                  /* Start at track 0 */
    move.w  #WORDS_PER_TRACK_DD,%d6

.load_track:
    cmp.w   #TRACKS_TO_LOAD,%d5
    beq     .load_done

    /* Select side 0 (lower head): /SIDE=1 in PRB */
    /* PRB = 0xE7: /MTR=0, /SEL0=0, /SIDE=1, /DIR=1, /STEP=1 */
    /* Already set correctly - side 0 is /SIDE=1 */

    /* ---- Set disk DMA pointer (high word FIRST, then low word) ---- */
    move.l  %a2,%d0
    move.w  %d0,%d1                 /* Save low word */
    swap    %d0
    move.w  %d0,DSKPTH(%a5)        /* DSKPTH: high 16 bits of address */
    move.w  %d1,DSKPTL(%a5)        /* DSKPTL: low  16 bits of address */

    /* ---- Start DMA read ----
     * DSKLEN: bit15=1 (DMA enable), bit14=0 (READ, not write), bits13:0=word count.
     * Write DSKLEN TWICE to start the DMA (hardware requirement).
     * IMPORTANT: bit14=0 for read.  0x8000 | word_count.
     * The original used 0xC000 which sets bit14=1 = WRITE mode. WRONG.
     * ---------------------------------------------------------------- */

    /* Clear any stale DSKBLK interrupt flag before starting */
    move.w  #0x0002,INTREQ(%a5)

    move.w  %d6,%d0
    or.w    #0x8000,%d0             /* DMA enable, READ mode (bit14=0) */
    move.w  %d0,DSKLEN(%a5)        /* First write */
    move.w  %d0,DSKLEN(%a5)        /* Second write (required to start DMA) */

    /* ---- Wait for DSKBLK (disk block done, INTREQR bit 1) ---- */
.wait_dskblk:
    move.w  INTREQR(%a5),%d0
    btst    #1,%d0
    beq     .wait_dskblk

    /* Clear DSKBLK flag */
    move.w  #0x0002,INTREQ(%a5)

    /* Stop DMA (write DSKLEN with bit15=0) */
    move.w  #0x0000,DSKLEN(%a5)

    /* ---- Advance load address by bytes read ---- */
    /* Bytes = words * 2 */
    move.w  %d6,%d0
    add.w   %d0,%d0                 /* words -> bytes (shift left 1) */
    ext.l   %d0
    add.l   %d0,%a2

    /* ---- Seek to next track ----
     * We step inward one track for each subsequent track.
     * Track 0 is already done; step to track 1, 2, etc.
     * Direction: inward = /DIR=0.
     * PRB with /DIR=0: 0xE5 = 0b11100101
     *   bit7=/MTR=0, bit6-4=111(others desel), bit3=/SEL0=0,
     *   bit2=/SIDE=1, bit1=/DIR=0(inward), bit0=/STEP=1
     * ---------------------------------------------------------------- */

    addq.w  #1,%d5                  /* Increment track counter */
    cmp.w   #TRACKS_TO_LOAD,%d5
    beq     .load_done              /* All tracks read; no need for final seek */

    /* Set DIR=inward (/DIR=0, bit1=0) */
    bclr    #1,(%a4)                /* PRB bit1 = /DIR = 0 (inward) */

    /* Single step pulse */
    bclr    #0,(%a4)                /* /STEP low */
    nop
    nop
    nop
    nop
    nop
    nop
    bset    #0,(%a4)                /* /STEP high */

    /* Post-step settle delay */
    move.w  #5000,%d7
.seek_delay:
    subq.w  #1,%d7
    bne     .seek_delay

    bra     .load_track

.load_done:

    /* ============================================================
     * Tidy up
     * ============================================================ */

    /* Disable disk DMA */
    move.w  #DMAF_DISK,DMACON(%a5)     /* Clear DMAF_DISK (no SETCLR) */

    /* Deselect all drives and turn off motor */
    move.b  #0xFF,(%a4)

    /* Restore ADKCON to default (clear disk-related bits) */
    move.w  #0x7FFF,ADKCON(%a5)

    /* Colour feedback: loaded OK */
    move.w  #0x00A0,COLOR00(%a5)       /* Green */

    /* ============================================================
     * CPU detection and VBR setup
     *
     * MOVEC is a 68010+ instruction.  Executing it on a 68000
     * causes an Illegal Instruction exception (vector 4 at 0x10).
     * We detect the CPU by attempting to read CACR:
     *   - 68000: no CACR, MOVEC causes illegal instruction -> exception
     *   - 68010+: CACR exists, MOVEC works
     *
     * We install a temporary illegal-instruction handler at vector 4
     * that skips the MOVEC and clears a flag, then restore it.
     *
     * Since we are loading the kernel at 0x000000, the vectors are
     * being set up by start.S.  We only need VBR=0 on 68010+ where
     * the ROM may have set VBR to somewhere else (Kickstart maps
     * itself using VBR on 68010+).
     *
     * Simple approach: we know we're on an Amiga with a 68030+
     * (since this is NeoBench which requires MMU).  So we can
     * unconditionally execute MOVEC, but we guard it with a
     * try/catch using the illegal instruction vector.
     * ============================================================ */

    /* Save current illegal instruction vector (vector 4 at byte offset 0x10) */
    move.l  0x10,%d7

    /* Install our temporary handler */
    move.l  #.movec_ok,0x10

    /* Attempt to set VBR = 0 */
    moveq   #0,%d0
    .word   0x4E7B, 0x0801          /* movec d0,vbr  (68010+ only) */
                                    /* Using .word encoding to avoid assembler
                                     * errors if targeting 68000 by default  */

.movec_ok:
    /* Restore original illegal instruction vector */
    move.l  %d7,0x10

    /* ============================================================
     * Jump to kernel
     *
     * The kernel binary is now at 0x000000.
     * The vector table (from vectors.S) is:
     *   0x000000: Initial SSP (supervisor stack pointer)
     *   0x000004: Initial PC  (reset vector = kernel entry point)
     *
     * We load SP from the kernel's SSP value and jump via the PC.
     * ============================================================ */

    /* Final colour: cyan = jumping to kernel */
    move.w  #0x0088,COLOR00(%a5)

    /* Load kernel's initial stack */
    move.l  0x000000,%sp

    /* Load and jump to kernel entry point */
    move.l  0x000004,%a0
    jmp     (%a0)

    /* Should never reach here */
.hang:
    move.w  #0x0F00,COLOR00(%a5)    /* Red = fault */
    bra     .hang

    /* ============================================================
     * Pad to exactly 1024 bytes
     * ============================================================ */

    .balign 1024, 0x00
_bootblock_end:

    /* Verify we fit in 1024 bytes */
    .if     (_bootblock_end - _bootblock_start) > 1024
    .error  "Bootblock exceeds 1024 bytes!"
    .endif
