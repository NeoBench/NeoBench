/*
 * Vertical blank interrupt: rewind the eight bitplane DMA pointers for
 * the field that is about to be scanned.
 *
 * BPLxPT is a running counter, not a base register.  Agnus advances each
 * plane pointer by the bytes it fetched on that line and nothing in the
 * hardware ever puts it back, so a program that writes the pointers once
 * shows frame 1 correctly, frame 2 from one plane further up chip RAM,
 * and every frame after that from further still.  The boot log decays
 * through the upper (zero) planes into raw memory and is unreadable
 * within a fifth of a second.
 *
 * Rewinding from the idle loop only helps once the kernel reaches the
 * idle loop -- the banner, the detection pass and the whole desktop
 * render all happen before it, which is why the boot log came out as
 * noise.  Doing it off the level-3 autovector instead puts the pointers
 * back once per *field* no matter what the CPU happens to be busy with.
 *
 * INTERLACE turns "once per field" into the whole job rather than half
 * of it.  A field only shows 256 of the 512 rows, and the modulo set in
 * amiga_display_init() advances each pointer two rows per fetched line,
 * so a field walks the even rows or the odd ones depending on where its
 * pointer started.  VPOSR bit 15 (LOF; graphics/display.i VPOSRLOF =
 * $8000) marks the long field and alternates with it, so it is exactly
 * the selector: the long field starts at row 0, the short one one plane
 * pitch further down.  Without it both fields would paint the same 256
 * rows and every other scanline of the desktop would go missing.
 *
 * Autovector level 3 is VERTB (NDK hardware/intbits.i: INTB_VERTB = 5,
 * level 3 together with COPER and BLIT).  AmigaOS's handler for that
 * vector is replaced by amiga_display_init(), which also masks every
 * other source out of INTENA, so this is the only thing that can arrive.
 *
 * The handler preserves every register it touches: the 68000 exception
 * frame saves only SR and PC, so an interrupt handler that lets D0-D7 or
 * A0-A6 go would corrupt whatever code was interrupted.
 *
 * NB_FB_BASE, NB_PLANE_PITCH and NB_PLANE_SIZE must match amiga.h:
 * $10000, 80 bytes, and 80 bytes x 512 rows = $A000.  Immediate shift
 * counts are 1..8 on a 68000, hence the split 8+7 below.
 */
    .text
    .global nb_vbl_isr

    .equ    NB_FB_BASE,     0x00010000
    .equ    NB_PLANE_PITCH, 0x00000050
    .equ    NB_PLANE_SIZE,  0x0000A000
    .equ    NB_VPOSR,       0x00dff004

nb_vbl_isr:
    movem.l %d0-%d7/%a0-%a6, -(%sp)

    move.w  #0x0020, 0x00dff09c        /* INTREQ: clear VERTB (bit 15 low)  */

    /*
     * d0 = field row offset.  VPOSR bit 15 is LOF; shift it down to a
     * 0/1, subtract one so the short field (LOF clear) becomes -1 and
     * the long field 0, then keep the plane pitch out of that mask.
     */
    moveq   #0, %d0
    move.w  NB_VPOSR, %d0              /* d0 = VPOSR                        */
    lsr.l   #8, %d0
    lsr.l   #7, %d0                    /* d0 = LOF (0 or 1)                 */
    subq.l  #1, %d0                    /* long -> 0, short -> -1            */
    and.l   #NB_PLANE_PITCH, %d0       /* d0 = 0 or 80                      */

    lea     0x00dff0e0, %a0            /* BPL1PTH, then +4 per plane        */
    move.l  #NB_FB_BASE, %d1
    add.l   %d0, %d1                   /* plane 0, field-adjusted           */
    moveq   #7, %d2                    /* dbra: eight planes                */
1:
    move.l  %d1, %d0
    swap    %d0                        /* d0 low word = plane address high  */
    move.w  %d0, (%a0)+                /* BPLxPTH                           */
    move.w  %d1, (%a0)+                /* BPLxPTL                           */
    add.l   #NB_PLANE_SIZE, %d1
    dbra    %d2, 1b

    /*
     * One tick per field for the taskbar uptime.  Counted here rather
     * than polled out of the idle loop because INTREQR is cleared by
     * this very handler a few microseconds after the hardware raises it
     * -- a poll almost never catches the bit set, and one that misses
     * runs to its spin bound.  Called before the registers come back so
     * the C ABI can take d0-d1/a0-a1.
     */
    bsr     nb_vbl_tick

    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte
