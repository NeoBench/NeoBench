#include <stdint.h>

#include "amiga.h"
#include "font8x8.h"
#include "../../kernel/include/kernel.h"

/*
 * Native Amiga chipset support for the NeoBench kernel.
 *
 * Target is AGA only (A1200/A4000). The console is a 320x256 lores display
 * driven by one bitplane and an embedded 8x8 font, so the kernel has a real
 * native video console instead of the old x86-PC VGA text buffer.
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
#define BPL1PTH     0x0e0
#define BPL1PTL     0x0e2
#define BPLCON0     0x100
#define BPLCON1     0x102
#define BPLCON2     0x104
#define BPLCON3     0x106
#define COLOR00     0x180
#define COLOR01     0x182
#define SERDAT      0x030
#define SERPER      0x032
#define SERDATR     0x018
#define INTREQ      0x09c               /* write here, read back at $01e */
#define INTREQR     0x01e
#define FMODE       0x1fc

/* SERDATR status bits, NDK hardware/custom.i */
#define SERDATF_TBE   0x2000U            /* transmit buffer empty    */
#define SERDATF_TSRE  0x1000U            /* shift register empty     */

/* DMA write bits, from NDK hardware/dmabits.i */
#define DMAF_SETCLR  0x8000U
#define DMAF_RASTER  0x0100U            /* bitplane DMA            */
#define DMAF_MASTER  0x0200U            /* master DMA enable       */

/* BPLCON0 bits, from NDK graphics/display.h */
#define COLORON      0x0200U
#define BPLCNT(n)    ((uint16_t)((n) & 0x7) << 12)  /* PLNCNTSHFT = 12 */

/* Vertical-blank request bit, NDK hardware/intbits.i */
#define INTF_VERTB   0x0020U

#define SERIAL_TX     0x00DFF030UL      /* FS-UAE serial capture port */

#define FRAMEBUF     0x00010000UL      /* bitplane base, 64-byte aligned */
#define FB_PITCH     40UL              /* 320 px / 8 = 40 bytes per row  */
#define FB_ROWS      256UL
#define FB_PLANES    8UL               /* planar layout: plane p sits    */
#define FB_PLANE     (FB_PITCH * FB_ROWS)  /* FB_PLANE bytes after p-1    */

#define TEXT_COLS    40UL
#define TEXT_ROWS    32UL

#define STATE_BASE   0x00007F00UL      /* console scratch in chip ram */
#define SCRATCH_COL  (*(volatile uint8_t *)(STATE_BASE + 0UL))
#define SCRATCH_ROW  (*(volatile uint8_t *)(STATE_BASE + 1UL))

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
 * Bring up a 320x256 lores display with a single bitplane on AGA.
 *
 * The two details that matter most here:
 *
 *  - DMAF_MASTER must be set alongside DMAF_RASTER. The bitplane channel bit
 *    is gated by the master enable, so writing $8100 alone leaves *no* DMA
 *    running and the screen stays black.
 *  - FMODE must be 0 so Agnus performs 16-bit fetches. With DDF 0x38/0xd0
 *    that is 20 words (40 bytes) per line, matching FB_PITCH. FMODE != 0
 *    would switch to 32-bit/64-byte fetches and read off the end of the row.
 */
void amiga_display_init(void)
{
    REG16(FMODE)   = 0;                /* 16-bit fetches (OCS-compatible) */
    REG16(BPLCON3) = 0;                /* normal colours, bank 0          */

    REG16(DIWSTRT) = 0x2c81;           /* vstart 44, hstart 0x81          */
    REG16(DIWSTOP) = 0x2cc1;           /* wraps -> 256 lines              */
    REG16(DDFSTRT) = 0x0038;
    REG16(DDFSTOP) = 0x00d0;

    REG16(BPL1PTH) = (uint16_t)(FRAMEBUF >> 16);
    REG16(BPL1PTL) = (uint16_t)(FRAMEBUF & 0xffffU);

    REG16(BPLCON0) = (uint16_t)(BPLCNT(1) | COLORON);  /* lores, 1 plane  */
    REG16(BPLCON1) = 0x0000;
    REG16(BPLCON2) = 0x0000;

    REG16(COLOR00) = 0x0000;           /* black background                */
    REG16(COLOR01) = 0x03f3;           /* P1 phosphor green (#33ff33):
                                        * 0x0RGB, so 0x0ff0 would be
                                        * R=F,G=F,B=0 (yellow)             */

    REG16(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER;

    SCRATCH_COL = 0;
    SCRATCH_ROW = 0;

    amiga_display_clear();
}

void amiga_display_clear(void)
{
    uint8_t *p = (uint8_t *)FRAMEBUF;
    uint32_t i;

    for (i = 0; i < FB_PITCH * FB_ROWS; i++)
        p[i] = 0;
}

/*
 * Wait for the vertical blank and rewind the bitplane DMA pointer.
 *
 * BPLxPT is a running counter, not a base register: Agnus advances it by the
 * number of bytes fetched on every line and nothing in the hardware ever
 * rewinds it.  A program that writes the pointer only once therefore fetches
 * frame 2 from FRAMEBUF+10240 and every frame after that from further up chip
 * RAM -- which is zeroed, so the screen shows nothing but the background
 * colour no matter what was drawn into the framebuffer (the CPU still reads
 * back its own writes, so the data looks fine).
 *
 * AmigaOS solves this with a copper list that MOVEs BPLxPTH/L at the top of
 * every frame; with no copper list running we do the same from the idle loop,
 * synchronised to the vertical-blank request bit.
 */
void amiga_display_vsync(void)
{
    uint32_t spins = 0;

    REG16(INTREQ) = INTF_VERTB;             /* ack the previous vblank */
    while ((REG16(INTREQR) & INTF_VERTB) == 0)
    {
        if (++spins > 4000000UL)            /* bounded: never hang the boot */
            break;
    }

    REG16(BPL1PTH) = (uint16_t)(FRAMEBUF >> 16);
    REG16(BPL1PTL) = (uint16_t)(FRAMEBUF & 0xffffU);
}

/*
 * Set one screen pixel to a planar colour index.
 *
 * The console font is drawn through this instead of whole bytes because the
 * glyphs are sheared (see amiga_putc): a sheared row no longer lines up with
 * a byte boundary, and the horizontal shift is different for every scanline.
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
 * Glyphs are rendered in italics: each scanline is shifted right by two
 * pixels at the cap height down to zero at the baseline (7-i)/3, the classic
 * bitmap-font shear.  Each row's cell is erased first, using exactly the
 * sheared footprint of that row, so a repaint clears the old glyph without
 * ever clipping the shear of the neighbouring cell (neighbour shear pixels
 * always land outside the footprint).
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
        const uint32_t x0 = (uint32_t)col * 8UL;
        const uint32_t y0 = (uint32_t)row * 8UL;

        for (i = 0; i < 8; i++)
        {
            /* Shear per scanline: 2 px at the cap height, 0 at baseline.
             * Table, not a divide: no libgcc in this freestanding link. */
            static const uint8_t shear_tab[8] = { 2, 2, 1, 1, 1, 0, 0, 0 };
            const uint32_t shear = shear_tab[i];
            uint32_t k;

            for (k = 0; k < 8; k++)
                fb_pixel(x0 + shear + k, y0 + i, 0);          /* erase   */

            for (k = 0; k < 8; k++)
            {
                if (glyph[i] & (uint8_t)(0x80U >> k))
                    fb_pixel(x0 + shear + k, y0 + i, 1);      /* phosphor */
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
