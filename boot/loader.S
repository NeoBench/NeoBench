/*=============================================================================
 * NeoBench Kernel - Boot Loader
 * Loads kernel from floppy (ADF) or ROM into Chip RAM and executes it.
 *
 * This is the bootblock code that goes in the first 1024 bytes of an ADF.
 * When the Amiga ROM loads this bootblock, it passes:
 *   A0 = Pointer to bootblock code in memory
 *   A1 = IORequest for trackdisk.device
 *   A6 = ExecBase (we don't use AmigaOS - just take over)
 *
 * For ROM boot: This code is not needed - the ROM image starts directly
 * at the reset vector with start.S
 *=============================================================================*/

    .section .text.bootblock, "ax"
    .global _bootblock_start

_bootblock_start:
    /* Bootblock header - recognized by Amiga ROM */
    .ascii  "DOS\0"                /* Disk type: OFS */
    .long   0x00000000             /* Checksum (filled by makeadf tool) */
    .long   0x00000370             /* Root block pointer */

    /*
     * At this point, the Amiga ROM has loaded us and is running.
     * We need to take over the machine completely.
     * Strategy:
     *   1. Disable AmigaOS completely (we're going bare-metal)
     *   2. Load rest of kernel from disk
     *   3. Jump to kernel entry point
     */

_bootblock_entry:
    /* Take over: supervisor mode, all interrupts off */
    move.w  #0x2700,%sr

    /* Kill AmigaOS - disable all DMA and interrupts */
    lea     0xDFF000,%a5           /* Custom chip base */
    move.w  #0x7FFF,0x096(%a5)    /* DMACON - disable all DMA */
    move.w  #0x7FFF,0x09A(%a5)    /* INTENA - disable all interrupts */
    move.w  #0x7FFF,0x09C(%a5)    /* INTREQ - clear all pending */

    /* Set up our stack at top of guaranteed 512KB Chip RAM */
    move.l  #0x080000,%sp

    /* Display a color to show we're alive */
    move.w  #0x0058,0x180(%a5)    /* COLOR00 = dark blue (NeoBench blue) */

    /*
     * Load kernel from disk using low-level disk access.
     * Kernel is stored starting at sector 2 (offset 1024) of the ADF.
     * We'll read it into 0x000400 (just after vector table space).
     *
     * Amiga floppy DMA:
     *   - Set DSKPT to destination buffer (must be in Chip RAM)
     *   - Set DSKSYNC for MFM sync word (0x4489)
     *   - Set DSKLEN for number of words to read + DMA enable
     *   - Wait for DSKBLK interrupt
     */

    /* Destination: 0x000000 (we'll overwrite vectors - kernel has its own) */
    move.l  #0x000000,%a4          /* Load address */

    /* We need to read the kernel track by track */
    /* 11 sectors per track, 512 bytes per sector = 5632 bytes per track */
    /* Skip track 0 (bootblock), start from track 0 sector 2 */

    /* First, select drive 0 and start motor */
    lea     0xBFD100,%a3           /* CIA-B PRB */
    move.b  #0xF7,(%a3)           /* SEL0=0 (select DF0), MTR=0 (motor on) */

    /* Wait for motor to spin up */
    move.l  #50000,%d7
.motor_wait:
    subq.l  #1,%d7
    bne     .motor_wait

    /* Step to track 0 first (home the head) */
    move.b  #0xFD,(%a3)           /* DIR=0 (toward track 0) */
.seek_track0:
    btst    #4,0xBFE001           /* CIA-A PRA bit 4: /TK0 */
    beq     .at_track0
    /* Step pulse */
    bclr    #0,(%a3)              /* STEP=0 */
    nop
    nop
    bset    #0,(%a3)              /* STEP=1 */
    /* Step delay */
    move.l  #3000,%d7
.step_delay:
    subq.l  #1,%d7
    bne     .step_delay
    bra     .seek_track0

.at_track0:
    /* Now read tracks using disk DMA */
    /* Set sync word */
    move.w  #0x4489,0x07E(%a5)    /* DSKSYNC = MFM sync */

    /* Enable disk DMA */
    move.w  #0x8210,0x096(%a5)    /* DMACON: set DMAEN + DSKEN */

    /* Read first 40KB of disk (enough for kernel) */
    /* That's about 7 tracks worth of data */
    move.l  %a4,%d0                /* Destination address */
    move.w  %d0,0x022(%a5)        /* DSKPTL */
    swap    %d0
    move.w  %d0,0x020(%a5)        /* DSKPTH */

    /* Read: 0x8000 | 0x4000 | word_count */
    /* 40KB = 20480 words */
    move.w  #0xC000|20480,0x024(%a5)  /* DSKLEN: DMA enable + length */
    move.w  #0xC000|20480,0x024(%a5)  /* Write twice to start (hardware requirement) */

    /* Wait for disk DMA complete (DSKBLK interrupt) */
.wait_disk:
    move.w  0x01E(%a5),%d0        /* INTREQR */
    btst    #1,%d0                 /* DSKBLK bit */
    beq     .wait_disk

    /* Clear interrupt */
    move.w  #0x0002,0x09C(%a5)    /* INTREQ: clear DSKBLK */

    /* Disable disk DMA */
    move.w  #0x0010,0x096(%a5)    /* DMACON: clear DSKEN */

    /* Turn off drive motor */
    move.b  #0xFF,(%a3)           /* Deselect all drives */

    /* Color feedback: loading done */
    move.w  #0x00A0,0x180(%a5)    /* COLOR00 = green = loaded */

    /* Kernel is now at 0x000000 */
    /* Set up the vector table pointer (VBR) if not 68000 */
    moveq   #0,%d0
    movec   %d0,%vbr              /* VBR = 0 (vectors at 0x000000) */

    /* Jump to kernel entry point */
    /* The kernel's vector table has SSP at offset 0, PC at offset 4 */
    move.l  0x000000,%sp          /* Load SSP from vector table */
    move.l  0x000004,%a0          /* Load entry point */
    jmp     (%a0)                  /* Boot NeoBench! */

    /* Pad bootblock to 1024 bytes */
    .balign 1024, 0x00
_bootblock_end:
