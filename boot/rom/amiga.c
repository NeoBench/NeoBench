#include <stdint.h>

#include "amiga.h"
#include "font8x8.h"
#include "../../kernel/include/kernel.h"

/*
 * Native Amiga chipset support for the NeoBench kernel.
 *
 * Target is AGA only (A1200/A4000). The console is a 640x512 interlaced
 * hires display driven by eight bitplanes and an embedded 8x8 font
 * stretched into 8x16 cells, so the kernel has a real native video
 * console instead of the old x86-PC VGA text buffer.
 *
 * Register names and bit values follow the AmigaOS 3.2 NDK headers
 * (hardware/custom.i, hardware/dmabits.i, graphics/display.h).
 */

#define CUSTOM_BASE 0x00DFF000UL
#define REG16(off)  (*(volatile uint16_t *)(CUSTOM_BASE + (off)))

/* Register offsets, from NDK hardware/custom.i */
#define DIWSTRT     0x08e
#define DIWSTOP     0x090
#define DDFSTRT     0x092
#define DDFSTOP     0x094
#define DMACON      0x096
#define INTENA      0x09a
#define INTREQ      0x09c               /* write here, read back at $01e */
#define INTREQR     0x01e
#define BPL1PTH     0x0e0
#define BPL1PTL     0x0e2
#define BPLPT(n)    (0x0e0U + (uint16_t)((n) << 2))   /* BPL1PT..BPL8PT */
#define BPLCON0     0x100
#define BPLCON1     0x102
#define BPLCON2     0x104
#define BPLCON3     0x106
#define BPL1MOD     0x108               /* modulo, odd planes (1,3,5,7) */
#define BPL2MOD     0x10A               /* modulo, even planes (2,4,6,8) */
#define COLOR00     0x180
#define COLOR01     0x182
#define SERDAT      0x030
#define SERPER      0x032
#define SERDATR     0x018
#define FMODE       0x1fc

/* SERDATR status bits, NDK hardware/custom.i */
#define SERDATF_TBE   0x2000U            /* transmit buffer empty    */
#define SERDATF_TSRE  0x1000U            /* shift register empty     */

/* DMA write bits, from NDK hardware/dmabits.i */
#define DMAF_SETCLR  0x8000U
#define DMAF_RASTER  0x0100U            /* bitplane DMA            */
#define DMAF_MASTER  0x0200U            /* master DMA enable       */
#define DMAF_COPPER  0x0002U            /* copper DMA              */

/* INTENA/INTREQ write bits, NDK hardware/intbits.i.  Bit 15 is the same
 * set/clear selector DMAF_SETCLR uses: high sets the bits named in the
 * low half, low clears them.  INTF_INTEN is the master enable. */
#define INTF_SETCLR  0x8000U
#define INTF_INTEN   0x4000U
#define INTF_VERTB   0x0020U

/*
 * BPLCON0 bits, from NDK graphics/display.h.
 *
 * MODE_640 ($8000) selects hires.  Eight bitplanes cannot be expressed in
 * the three-bit plane-count field (max 7, and 7 is "reserved"), so AGA puts
 * the high bit of the count at bit 4: BPU=0 plus $0010 means eight planes.
 * Anything else with bit 4 set would count as more than eight and the
 * chipset shows nothing at all (the emulator rejects it as >8 planes).
 */
#define MODE_640     0x8000U
#define COLORON      0x0200U
#define BPLCNT_AGA8  0x0010U
#define INTERLACE    0x0004U            /* graphics/display.h: INTERLACE */

#define SERIAL_TX     0x00DFF030UL      /* FS-UAE serial capture port */

/* Planar frame buffer geometry, from amiga.h */
#define FRAMEBUF     NB_FB_BASE
#define FB_PITCH     NB_PLANE_PITCH     /* 640 px / 8 = 80 bytes per row */
#define FB_ROWS      NB_SCREEN_H
#define FB_PLANES    8UL
#define FB_PLANE     NB_PLANE_SIZE

#define TEXT_COLS    (NB_SCREEN_W / 8UL)        /* 80 */
#define TEXT_ROWS    (NB_SCREEN_H / NB_CELL_H)  /* 32 */
#define TEXT_YS      2UL   /* font row -> screen rows (8 px font in a 16 px cell) */

#define STATE_BASE   0x00007F00UL      /* console scratch in chip ram */
#define SCRATCH_COL  (*(volatile uint8_t *)(STATE_BASE + 0UL))
#define SCRATCH_ROW  (*(volatile uint8_t *)(STATE_BASE + 1UL))
#define SCRATCH_FG   (*(volatile uint8_t *)(STATE_BASE + 2UL))

/* Set at the end of amiga_display_init; see amiga_display_ready(). */
static uint8_t display_ready;

void amiga_serial_putc(char c)
{
    volatile uint16_t * const sr =
        (volatile uint16_t *)(CUSTOM_BASE + SERDATR);
    uint32_t spins = 0;

    /*
     * SERPER is armed once in _start. The emulator keeps a byte queued until
     * the shift register drains (about 14 scanlines at 9600 baud) and any
     * further write during that window overwrites the queued byte, so pace
     * output by polling SERDATR instead of by a fixed delay: the port is only
     * loaded once both the buffer and the shift register report empty.
     *
     * The wait is bounded so a stalled transmitter can never hang the boot.
     */
    while (((*sr) & (SERDATF_TBE | SERDATF_TSRE)) !=
           (SERDATF_TBE | SERDATF_TSRE))
    {
        if (++spins > 400000UL)
            break;
    }

    *(volatile uint16_t *)(CUSTOM_BASE + SERDAT) = (uint16_t)(uint8_t)c;
}

/*
 * Program the serial transmitter bit period.
 *
 * SERPER is not reset to a usable value, and the emulator derives its
 * output pacing from it: left alone it sits at period 16 (baud 256000,
 * hsyncs 1) which FS-UAE cannot frame, so every byte after the first few
 * comes out as garbage. Period 371 is the standard 9600 baud divisor that
 * AmigaOS uses, and it is what the emulator accepts.
 */
void amiga_serial_init(void)
{
    REG16(SERPER) = 371;
}

/*
 * Arm the level-3 vertical blank so BPLxPT is rewound once per frame.
 *
 * AmigaOS's handler for that vector is replaced outright, and every other
 * source is masked out of INTENA first -- the ROM we chainloaded from is
 * still installed and its interrupt servers have no business re-arming
 * anything now that this code owns the display.
 *
 * INTENA's bit 15 is set/clear, so $7fff written with it low turns the
 * whole mask off (writing zero would set nothing and clear nothing).
 * The vector is loaded only after that, so there is no window in which a
 * stale level-3 request could dispatch through a half-written pointer.
 */
extern void nb_vbl_isr(void);

/*
 * Uptime.  nb_vbl_tick() runs from vbl.S once per field -- PAL is 50
 * fields a second, so 50 ticks make one second.  Both live at file scope
 * with no initializer, which is what puts them in .bss in chip RAM: a
 * mutable global with an initializer would be assembled into .data, and
 * rom.ld maps .data into the ROM at $FC0000, where every store is
 * dropped on the floor.
 */
volatile uint32_t nb_secs;
static uint8_t nb_field_phase;

void nb_vbl_tick(void)
{
    if (++nb_field_phase >= 50U)
    {
        nb_field_phase = 0;
        nb_secs++;
    }
}

static void nb_vbl_install(void)
{
    REG16(INTENA) = 0x7fffU;            /* mask every source               */
    REG16(INTREQ) = 0x7fffU;            /* ack whatever was already pending */

    /*
     * The level-3 autovector, written from here rather than as a C
     * store: a constant address this low has no object behind it, which
     * is exactly what -Warray-bounds is trying to say, and the vector
     * table is one of the few things in this address space that is
     * genuinely just a raw location.
     */
    __asm__ volatile ("move.l %0, 0x0000006C"
                      :
                      : "r" (nb_vbl_isr)
                      : "memory");

    REG16(INTENA) = INTF_SETCLR | INTF_INTEN | INTF_VERTB;

    /*
     * Supervisor mode with the interrupt priority mask at 2: a level-3
     * request is taken only when the mask is below its own level, and
     * nothing below 3 can arrive anyway with INTENA cleared.
     */
    __asm__ volatile ("move.w #0x2200, %sr");
}

/*
 * Bring up an interlaced hires 640x512 display with eight bitplanes
 * (256 colours) on AGA.
 *
 * The details that matter:
 *
 *  - DMAF_MASTER must be set alongside DMAF_RASTER. The bitplane channel bit
 *    is gated by the master enable, so writing $8100 alone leaves *no* DMA
 *    running and the screen stays black.
 *  - FMODE = 1 (32-bit fetch) is the only fetch mode whose cycle diagram
 *    grants eight plane fetches per eight-cycle unit in hires; at 16-bit
 *    fetch (FMODE = 0) hires tops out at four planes, so AGA 256 colours at
 *    640 px is impossible without it.
 *  - DDF 0x34/0xcc then yields (0xcc-0x34)/8 + 1 = 20 units of 4 bytes =
 *    80 bytes fetched per plane per line, exactly FB_PITCH, so a plane
 *    pointer advances one row for free and the modulo only has to skip
 *    the *other* field's row: +80.  That is what makes 512 lines fit in
 *    256 lines per field.
 *  - INTERLACE (BPLCON0 bit 2) tells Denise/Agnus the two fields weave
 *    into one picture.  DIW does not change: each field still shows 256
 *    lines starting at 44, and the interlace bit is the only difference
 *    from the progressive 640x256 mode.
 *
 * The order inside this function is the other half of the fix.  Raster
 * DMA is stopped first: the picture the chipset is scanning while we
 * reprogram it belongs to whatever ran before, and FMODE/DDF/BPLCON0
 * change under it means one frame of that memory read as an eight-plane
 * hires image -- raw noise in the new palette, which is what the boot log
 * used to start with.  With the DMA off the display is solid COLOR00, and
 * the palette below makes that black.  The frame buffer is then cleared
 * *before* the fetchers are restarted, and the vertical blank is armed
 * before that, so the first frame fetched is already a frame the pointers
 * will be rewound for.
 */
void amiga_display_init(void)
{
    uint32_t i;

    REG16(DMACON) = DMAF_RASTER | DMAF_COPPER;  /* bit 15 low: stop both   */

    REG16(FMODE)   = 1;                /* 32-bit fetches (8 planes/hires)  */
    REG16(BPLCON3) = 0;                /* colour bank 0, high-nibble pass  */

    REG16(DIWSTRT) = 0x2c81;           /* vstart 44, hstart 0x81          */
    REG16(DIWSTOP) = 0x2cc1;           /* wraps -> 256 lines per field     */
    /*
     * DDFSTRT is one 32-bit fetch unit (0x08, = 32 hires pixels at FMODE 1)
     * earlier than the classic hires $3c: with eight 32-bit plane fetches the
     * data window starts 32 pixels after the DIW opens, which clipped the
     * rightmost 32 columns of the framebuffer and left a 32-pixel backdrop
     * gap on the left.  $34 aligns the fetched data with DIW exactly
     * ($81..$c1), so all 640 columns show.
     */
    REG16(DDFSTRT) = 0x0034;
    REG16(DDFSTOP) = 0x00cc;

    /* One plane pitch of modulo: after 256 fetch lines the pointer has
     * walked all 512 rows instead of the 256 that were fetched. */
    REG16(BPL1MOD) = (uint16_t)FB_PITCH;
    REG16(BPL2MOD) = (uint16_t)FB_PITCH;

    for (i = 0; i < FB_PLANES; i++)
    {
        uint32_t base = FRAMEBUF + i * FB_PLANE;
        REG16(BPLPT(i))      = (uint16_t)(base >> 16);
        REG16(BPLPT(i) + 2U) = (uint16_t)(base & 0xffffU);
    }

    REG16(BPLCON0) = (uint16_t)(MODE_640 | COLORON | BPLCNT_AGA8 | INTERLACE);
    REG16(BPLCON1) = 0x0000;
    REG16(BPLCON2) = 0x0000;

    /* Console palette: black ground, phosphor green, and the two status
     * colours the boot self-test reports with. */
    amiga_set_color(NB_COL_BLACK, 0x00, 0x00, 0x00);
    amiga_set_color(NB_COL_GREEN, 0x33, 0xff, 0x33);
    amiga_set_color(NB_COL_RED,   0xff, 0x50, 0x40);
    amiga_set_color(NB_COL_AMBER, 0xff, 0xb0, 0x00);
    amiga_set_color(NB_COL_WHITE, 0xe8, 0xf0, 0xf8);

    SCRATCH_COL = 0;
    SCRATCH_ROW = 0;
    SCRATCH_FG  = NB_COL_GREEN;         /* phosphor green until told else */

    amiga_display_clear();              /* nothing fetched before this     */
    nb_vbl_install();                   /* rewind every frame from here on */

    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER;
    display_ready = 1;
}

/*
 * 1 once the display engine has been programmed and the frame buffer
 * cleared.  The mode registers are write-only, so this flag is the only
 * honest answer to "is the display up" for the boot self-test.
 */
int amiga_display_ready(void)
{
    return display_ready;
}

/*
 * Write one of the AGA's 256 hardware palette entries at full 8-bit depth.
 *
 * Denise keeps eight banks of 32 registers. BPLCON3 bits 15..13 select the
 * bank and bit 9 selects which nibble of each channel the write lands in:
 * clear writes the high nibble (the value is replicated across the byte),
 * set merges the low nibble into it. High pass first, then low pass, or the
 * second write would have nothing to merge into.
 */
void amiga_set_color(unsigned idx, uint8_t r, uint8_t g, uint8_t b)
{
    const unsigned bank = (idx >> 5) & 7U;
    const unsigned num  = idx & 31U;
    const uint16_t reg  = (uint16_t)(COLOR00 + num * 2U);

    REG16(BPLCON3) = (uint16_t)(bank << 13);
    REG16(reg) = (uint16_t)(((uint16_t)(r >> 4) << 8) |
                            ((uint16_t)(g >> 4) << 4) | (b >> 4));

    REG16(BPLCON3) = (uint16_t)((bank << 13) | 0x0200U);
    REG16(reg) = (uint16_t)(((uint16_t)(r & 15U) << 8) |
                            ((uint16_t)(g & 15U) << 4) | (b & 15U));
}

void amiga_display_clear(void)
{
    uint8_t *p = (uint8_t *)FRAMEBUF;
    uint32_t i;

    for (i = 0; i < FB_PLANE * FB_PLANES; i++)
        p[i] = 0;
}

/*
 * Wait for the next vertical blank.
 *
 * BPLxPT is a running counter, not a base register: Agnus advances it by
 * the number of bytes fetched on every line and nothing in the hardware
 * ever rewinds it.  That rewind belongs to the level-3 handler now (see
 * vbl.S), because it has to pick the field's starting row as well and it
 * has to run whatever the CPU happens to be busy with.  Acknowledging
 * INTF_VERTB from here would race the handler -- clear the request before
 * it dispatches and that field is never rewound -- so this only watches
 * the request bit.
 */
void amiga_display_vsync(void)
{
    uint32_t spins = 0;

    while ((REG16(INTREQR) & INTF_VERTB) == 0)
    {
        if (++spins > 4000000UL)            /* bounded: never hang the boot */
            break;
    }
}

/*
 * Stop (hold != 0) or restart the bitplane DMA around a full-frame repaint.
 *
 * gfx_present() rewrites every row of the planar frame buffer.  With the
 * fetchers still running, the beam crosses a frame that is half the old
 * picture and half the new one, and each of those rows is read through a
 * palette that no longer matches it -- so the transition is raw noise for
 * the whole time the pack takes.  Stopping raster DMA leaves the display
 * at COLOR00 instead; the palette has already been uploaded by then, so
 * the screen goes to the new wallpaper colour and simply stays there until
 * the frame is complete.
 */
void amiga_display_hold(int hold)
{
    if (hold)
        REG16(DMACON) = DMAF_RASTER;                   /* bit 15 low: stop  */
    else
        REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER;
}

/*
 * Set the colour index the console inks subsequent glyphs with.
 *
 * The palette itself is programmed once in amiga_display_init; this only
 * chooses which of those entries the next characters use, so the boot
 * self-test can report ok/fail/warning in green/red/amber on one screen.
 */
void amiga_set_fg(unsigned idx)
{
    SCRATCH_FG = (uint8_t)idx;
}

/*
 * Set one screen pixel to a planar colour index.
 *
 * The console font is drawn through this instead of whole bytes because each
 * font row is painted TEXT_YS times to stretch the 8x8 font over the 8x16
 * cell, and because the cell has to be erased before it is inked.  Keeping
 * erase and ink on one path means a repaint cannot leave a stale pixel.
 */
static void fb_pixel(uint32_t x, uint32_t y, uint8_t idx)
{
    uint32_t p;

    if (x >= (uint32_t)TEXT_COLS * 8UL || y >= FB_ROWS)
        return;

    for (p = 0; p < FB_PLANES; p++)
    {
        uint8_t *dst = (uint8_t *)(FRAMEBUF + p * FB_PLANE) + y * FB_PITCH
                       + (x >> 3);
        uint8_t mask = (uint8_t)(0x80U >> (x & 7U));

        if ((idx >> p) & 1U)
            *dst |= mask;
        else
            *dst &= (uint8_t)~mask;
    }
}

/*
 * Draw one character to the console and mirror it to the serial port, so
 * boot progress is observable without needing a screenshot.
 *
 * Glyphs are drawn upright.  The cell is erased first, so a repaint clears
 * the old glyph and cannot disturb the neighbouring cell.
 *
 * The raster is 512 lines but the console keeps its 80x32 grid, so each
 * font row is painted twice: the cell is 8x16 and the 8x8 font is
 * stretched over it.  That preserves both the 80-column message budget
 * and the fraction of the screen the boot log fills.
 */
void amiga_putc(char c)
{
    uint8_t col = SCRATCH_COL;
    uint8_t row = SCRATCH_ROW;
    uint32_t i;

    if (c == '\n')
        amiga_serial_putc('\r');
    amiga_serial_putc(c);

    if (c == '\n')
    {
        col = 0;
        row++;
        if (row >= TEXT_ROWS)
            row = TEXT_ROWS - 1;
        SCRATCH_COL = col;
        SCRATCH_ROW = row;
        return;
    }

    if (c == '\r')
    {
        SCRATCH_COL = 0;
        return;
    }

    {
        const uint8_t *glyph = font8x8[(uint8_t)c];
        const uint8_t fg = SCRATCH_FG;
        const uint32_t x0 = (uint32_t)col * 8UL;
        const uint32_t y0 = (uint32_t)row * NB_CELL_H;

        for (i = 0; i < 8; i++)
        {
            const uint32_t yl = y0 + i * TEXT_YS;
            uint32_t k, d;

            for (k = 0; k < 8; k++)
            {
                /* erase */
                for (d = 0; d < TEXT_YS; d++)
                    fb_pixel(x0 + k, yl + d, NB_COL_BLACK);
            }

            for (k = 0; k < 8; k++)
            {
                if (glyph[i] & (uint8_t)(0x80U >> k))
                {
                    for (d = 0; d < TEXT_YS; d++)
                        fb_pixel(x0 + k, yl + d, fg);               /* ink */
                }
            }
        }
    }

    col++;
    if (col >= TEXT_COLS)
    {
        col = 0;
        row++;
        if (row >= TEXT_ROWS)
            row = TEXT_ROWS - 1;
    }

    SCRATCH_COL = col;
    SCRATCH_ROW = row;
}

extern void kernel_main(const nb_bootinfo_t *boot);

/*
 * Boot entry point reached from the kickstart-style ROM header.
 *
 * Bring the display up first so that everything printed afterwards is
 * visible, then hand control to the NeoBench kernel.
 */
void rom_main(void)
{
    static const char bootline[] = "NeoBench rom_main\n";
    const char *s;
    static const nb_bootinfo_t bootinfo =    {
        .magic        = NB_BOOT_MAGIC,
        .version      = NB_BOOT_VERSION,
        .ram_size     = 0x00080000UL,
        .kernel_base  = 0x00FC0000UL,
        .kernel_size  = 0x00000000UL,
        .boot_device  = 0x00000000UL,
        .framebuffer  = FRAMEBUF,
        .cmdline      = 0x00000000UL,
    };

    for (s = bootline; *s; s++)
        amiga_serial_putc(*s);

    amiga_serial_init();
    amiga_serial_putc('I');            /* serial programmed */

    amiga_display_init();
    amiga_serial_putc('D');            /* display initialised */

    kernel_main(&bootinfo);

    for (;;)
        __asm__ volatile ("nop");
}
