/*
 * zz9000.device -- find MNT's ZZ9000, put it on the bus, start the
 * driver, and ask the card whether the AX module is clipped to it.
 *
 * Three steps, in the order the hardware requires them:
 *
 *   1.  Read the autoconfig window (zorro.c).  Both of them: a Zorro II
 *       card describes itself at $E80000 and a Zorro III card at
 *       $FF000000, so a scan of one is half a scan.  The Zorro III
 *       window is only read when the address bus is known to be 32 bits
 *       wide, because on a 24-bit machine that address is a truncation
 *       of $000000 and the scan would read chip RAM and call it a ROM.
 *
 *   2.  Give it an address.  A card in the window has not been given
 *       one -- that is what being in the window means -- and without
 *       one there are no registers to read, no AX to detect and no
 *       driver to bind, only a ROM.  The address comes from the same
 *       arithmetic the memory probe already did: the first window of
 *       the right size, above the floor for that bus, that holds no
 *       memory.  Every megabyte it asks about is a megabyte the probe
 *       has already written its two patterns into at the same address,
 *       so configuring a card costs no access the boot did not make
 *       anyway.  A card that is found and cannot be given a window is
 *       reported as exactly that rather than as absent.
 *
 *   3.  Read the card's own registers: the firmware version, which says
 *       that a ZZ9000 rather than something else is answering at that
 *       address, and the audio configuration word, whose low bit is the
 *       AX module fitted.  Firmware 2.8 is the ABI these offsets belong
 *       to; a version is believed when it looks like a version, so an
 *       open bus reading $FFFF or $0000 is not a card at 65535.
 *
 * The identity match in step 1 is deliberately exact -- manufacturer,
 * product and the reserved byte, all three -- because step 2 writes to
 * the autoconfig window, and a write to that window moves a board.  It
 * is the one thing in this file that cannot be undone without a reset,
 * so nothing short of a card that has stated its own name gets one.
 */
#include <stdint.h>
#include "amiga.h"
#include "probe.h"
#include "zorro.h"
#include "zz9000.h"

/* The two autoconfig windows, and how much of each is worth reading. */
#define NB_AUTO_Z2      0x00e80000UL
#define NB_AUTO_Z3      0xff000000UL
#define NB_SLOTS        16u

/* Control bytes at the end of the window.  Writing one of these is what
 * takes a board out of the window and gives it an address; the offset
 * and the field width are the Amiga's, not this system's invention. */
#define ZC_Z3_BASE      0x44       /* word write: address >> 16        */
#define ZC_Z2_LO        0x4a       /* byte write: low address nibble   */
#define ZC_Z2_HI        0x48       /* byte write: address bits 23..16  */

/* Register file, offsets from the card's base address. */
#define ZZ_REG_FW_VERSION   0x00c0u
#define ZZ_REG_AUDIO_CONFIG 0x00f4u   /* bit 0: AX fitted              */

/* Where each bus's address space begins, and where it stops being a
 * range this system has walked (probe.h's own limit). */
#define ZZ_Z3_FLOOR      0x40000000UL
#define ZZ_Z2_FLOOR      0x00200000UL
#define ZZ_Z2_END        0x00a00000UL

static struct nb_zz9000 zz;          /* uninitialised: this is .bss    */

/* ------------------------------------------------------------------ *
 * Serial read-out -- development only, never shown on screen
 * ------------------------------------------------------------------ */

static void s_put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void s_u(unsigned v)
{
    char buf[11];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do
    {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v);
    s_put(&buf[i]);
}

static void s_x(unsigned v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    int i = (int)sizeof(buf);

    buf[--i] = '\0';
    do
    {
        buf[--i] = hex[v & 15u];
        v >>= 4;
        digits--;
    } while (v || digits > 0);
    s_put(&buf[i]);
}

/*
 * The first `n' bytes of a window, undecoded.  What the decoder says
 * about a window is only as good as what the window held, so the wire
 * gets both: the judgement and the bytes it was made from.
 */
static void s_raw(const volatile uint8_t *p, unsigned n)
{
    static const char hex[] = "0123456789abcdef";
    unsigned i;

    for (i = 0; i < n; i++)
    {
        amiga_serial_putc(hex[p[i] >> 4]);
        amiga_serial_putc(hex[p[i] & 15u]);
    }
}

/* ------------------------------------------------------------------ *
 * Finding a window
 * ------------------------------------------------------------------ */

/*
 * Does any megabyte of this window hold memory?
 *
 * The test and the addresses are the memory probe's own: the last long
 * of the megabyte, two patterns, both re-read.  A megabyte that is free
 * floats and fails it; a megabyte that is RAM passes; and a megabyte
 * that is somebody's registers fails it too, which is why the window
 * has to be asked about only where the map has already been walked --
 * below NB_MEM_TOP_MB -- so that the writes this makes are the writes
 * the boot has already made at these same addresses.
 */
static int window_free(uint32_t addr, uint32_t size)
{
    unsigned mb0 = addr >> 20;
    unsigned mb1 = (addr + size - 1u) >> 20;
    unsigned mb;

    for (mb = mb0; mb <= mb1; mb++)
        if (nb_probe_ram(((uint32_t)mb << 20) | 0xffffcul))
            return 0;
    return 1;
}

/*
 * The address to give the card, or 0 when there is none to be had.
 *
 * Aligned to the card's own size, because that is how the bus hands out
 * address space and how the card decodes it, and above every window the
 * probe found memory in -- $40000000 is where Zorro III space starts,
 * and a card is never placed underneath RAM that is already there.
 */
static uint32_t pick_base(uint8_t zorro, uint32_t size)
{
    uint32_t floor, end, addr, top;

    if (size == 0 || size > (uint32_t)(NB_MEM_TOP_MB) * 0x100000ul)
        return 0;

    if (zorro == 3)
    {
        if (!nb_probe_bus())
            return 0;               /* 24-bit: that window is not there */
        floor = ZZ_Z3_FLOOR;
        end = (uint32_t)NB_MEM_TOP_MB * 0x100000ul;
    }
    else
    {
        floor = ZZ_Z2_FLOOR;
        end = ZZ_Z2_END;
    }

    addr = (floor + size - 1u) & ~(size - 1u);
    top = addr;
    while (top < end && top + size <= end)
    {
        if (window_free(top, size))
            return top;
        top += size;
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * The card's registers
 * ------------------------------------------------------------------ */

static int fw_plausible(uint16_t fw)
{
    /* A version has a small major and a byte of minor: an open bus
     * answers $FFFF and pulled-down lines answer $0000, and neither is
     * a firmware revision. */
    return fw != 0 && fw != 0xffffu && (unsigned)(fw >> 8) <= 9u;
}

static void configure(const struct nb_zdev *d, uint32_t base)
{
    volatile uint8_t *win =
        (volatile uint8_t *)(d->zorro == 3 ? NB_AUTO_Z3 : NB_AUTO_Z2);

    if (d->zorro == 3)
    {
        *(volatile uint16_t *)(win + ZC_Z3_BASE) = (uint16_t)(base >> 16);
    }
    else
    {
        /* The low nibble register first: the high byte is what the
         * board acts on. */
        win[ZC_Z2_LO] = 0;
        win[ZC_Z2_HI] = (uint8_t)(base >> 16);
    }
}

/* ------------------------------------------------------------------ *
 * The probe
 * ------------------------------------------------------------------ */

const struct nb_zz9000 *nb_zz9000_probe(void)
{
    static int done;
    struct nb_zdev boards[4];
    unsigned n = 0, i;
    unsigned slots = 0;

    if (done)
        return &zz;

    /*
     * Both windows, always: the scan has to answer "what is on the bus"
     * rather than "what is on the part of the bus I happened to look
     * at", and a Zorro II card and a Zorro III card never appear in the
     * same one.
     */
    slots = nb_zorro_scan((const volatile uint8_t *)NB_AUTO_Z2,
                          NB_SLOTS, boards, 4);
    n = slots;
    if (n < 4 && nb_probe_bus())
    {
        n += nb_zorro_scan((const volatile uint8_t *)NB_AUTO_Z3,
                           NB_SLOTS, boards + n, 4u - n);
    }

    for (i = 0; i < n; i++)
    {
        const struct nb_zdev *d = &boards[i];

        if (d->manuf != NB_ZZ9000_MANUF)
            continue;
        if (d->product != NB_ZZ9000_PRODUCT_Z2 &&
            d->product != NB_ZZ9000_PRODUCT_Z3 &&
            d->product != NB_ZZ9000_PRODUCT_Z3_256)
            continue;

        zz.present = 1;
        zz.product = d->product;
        zz.zorro = d->zorro;
        zz.size = d->size;
        break;
    }

    if (zz.present)
    {
        zz.base = pick_base(zz.zorro, zz.size);
        if (zz.base)
        {
            configure(&boards[i], zz.base);
            zz.fw = *(volatile uint16_t *)(zz.base + ZZ_REG_FW_VERSION);
            if (fw_plausible(zz.fw))
            {
                uint16_t ax =
                    *(volatile uint16_t *)(zz.base + ZZ_REG_AUDIO_CONFIG);

                zz.ax = (ax & 1u) ? 1 : 0;
                zz.bound = 1;
            }
        }
    }

    /* One line on the serial port: what was in the window, what was
     * asked for, and what answered.  The screen gets the judgement, the
     * wire gets the numbers. */
    s_put(">zz slots=");
    s_u(slots);
    s_put(" present=");
    s_u((unsigned)zz.present);
    s_put(" z2/z3=");
    s_u((unsigned)zz.zorro);
    s_put(" product=");
    s_u((unsigned)zz.product);
    s_put(" size=");
    s_u(zz.size);
    s_put(" base=$");
    s_x(zz.base, 8);
    s_put(" fw=$");
    s_x((unsigned)zz.fw, 4);
    s_put(" bound=");
    s_u((unsigned)zz.bound);
    s_put(" ax=");
    s_u((unsigned)zz.ax);
    if (n)
    {
        s_put(" first=type$");
        s_x(boards[0].type, 2);
        s_put(" prod$");
        s_x(boards[0].product, 2);
        s_put(" manuf$");
        s_x(boards[0].manuf, 4);
        s_put(" flags$");
        s_x(boards[0].flags, 2);
        s_put(" size=");
        s_u(boards[0].size);
    }
    s_put("\r\n");

    /* The bytes the judgement was made from: two windows, twenty-four
     * bytes each -- er_Type, product, flags, reserved and the
     * manufacturer, which is the whole of what identifies a board. */
    s_put(">zzz $E80000=");
    s_raw((const volatile uint8_t *)NB_AUTO_Z2, 24);
    if (nb_probe_bus())
    {
        s_put(" $FF000000=");
        s_raw((const volatile uint8_t *)NB_AUTO_Z3, 24);
    }
    s_put("\r\n");

    done = 1;
    return &zz;
}
