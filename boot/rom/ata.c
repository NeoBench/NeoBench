/*
 * Gayle IDE probe: controller, disk and ATAPI CD-ROM detection.
 *
 * NeoBench runs bare metal with nothing under it, so the boot log has to
 * answer "is there a disk?" the way a kernel does -- by talking to the
 * hardware.  The ATA task file is programmed directly and the answer comes
 * back from IDENTIFY DEVICE or IDENTIFY PACKET DEVICE, which is what turns
 * a driver line in the log from an assertion into a measurement.
 *
 * Every wait in this file is bounded.  A drive that is missing, slow or
 * wedged has to answer "no device" rather than hold the boot open, and the
 * bounds are written so that the worst case costs a few tens of
 * milliseconds -- and only when something really is sitting on the bus.
 */
#include <stdint.h>
#include "ata.h"

/*
 * A1200 task file.  Gayle puts the eight 8-bit registers on the upper data
 * lane, so each one is reached at an ODD byte address, while the 16-bit data
 * register sits at the even base.  An A600 puts the same eight registers on
 * the even addresses instead -- NeoBench targets the A1200 only.
 *
 *      $DA0000  data             16 bit
 *      $DA0005  error  / features
 *      $DA0009  sector count
 *      $DA000D  sector number
 *      $DA0011  cylinder low
 *      $DA0015  cylinder high
 *      $DA0019  device/head
 *      $DA001D  status  / command   read = status, write = command
 */
#define ATA_DATA   0x00da0000UL
#define ATA_CYLL   0x00da0011UL
#define ATA_CYLH   0x00da0015UL
#define ATA_DHEAD  0x00da0019UL
#define ATA_STAT   0x00da001dUL

#define ST_BSY  0x80                 /* busy: every other bit is undefined */
#define ST_DRQ  0x08                 /* data requested: 256 words pending  */
#define ST_ERR  0x01                 /* command failed                     */

#define CMD_IDENTIFY        0xec
#define CMD_IDENTIFY_PACKET 0xa1

/*
 * A bus with no drive on it floats, so the status register reads back as a
 * steady $FF or $00 instead of as a device.  Both are treated as empty: a
 * floating line can never be allowed to look like a device that has not
 * cleared BSY yet, or a missing drive would spin for the full bound.
 */
#define BUS_FLOAT0  0x00
#define BUS_FLOAT1  0xff

/*
 * Poll bounds, in bus reads.  Gayle is PIO mode 0 and its reads are slow, so
 * a few tens of thousands of reads is already tens of milliseconds;
 * IDENTIFY itself answers in well under a millisecond.
 */
#define POLL_PRESENT  40000U         /* nobody has claimed the bus yet     */
#define POLL_BSY     200000U         /* device selected, settling          */
#define POLL_DRQ     400000U         /* IDENTIFY result coming             */

static int floating(uint8_t st)
{
    return st == BUS_FLOAT0 || st == BUS_FLOAT1;
}

/*
 * The most recent status read on the task file.  Kept per unit at the point
 * the probe finishes, so an empty second unit cannot overwrite what the
 * first one saw -- the boot log has to report the unit it talked about.
 *
 * Deliberately zero-initialized: a global carrying a non-zero initializer
 * is emitted into .data, and rom.ld places .data in the ROM at $FC0000
 * where hardware drops the write, so the variable would never change.
 * Zero init lands it in .bss in chip RAM instead; nb_ata_identify() primes
 * it with the floating-bus value before each probe so that "never read"
 * and "read $00" stay distinguishable.
 */
static uint8_t last_status;

/*
 * Select a unit and wait for it to become addressable.  Returns 0 when no
 * device claimed the bus, or when the one that did never cleared BSY.
 */
static int select_unit(unsigned unit)
{
    volatile uint8_t *dh = (volatile uint8_t *)ATA_DHEAD;
    unsigned i;
    uint8_t st = BUS_FLOAT1;

    for (i = 0; i < POLL_PRESENT; i++)
    {
        st = *(volatile uint8_t *)ATA_STAT;
        last_status = st;
        if (!floating(st))
            break;
    }
    if (floating(st))
        return 0;

    *dh = (uint8_t)(unit ? 0xb0 : 0xa0);        /* $A0 master, $B0 slave  */

    for (i = 0; i < POLL_BSY; i++)
    {
        st = *(volatile uint8_t *)ATA_STAT;
        last_status = st;
        if (floating(st))
            return 0;
        if ((st & ST_BSY) == 0)
            return 1;
    }
    return 0;
}

/*
 * Issue one IDENTIFY flavour and wait for its result.
 *
 * DRQ may only be believed once BSY has cleared -- while BSY is set every
 * other bit is undefined, which is the classic way to get an ATA driver
 * wrong -- and an error without DRQ means the device refused the command.
 */
static int identify_with(uint8_t opcode)
{
    volatile uint8_t *cmd = (volatile uint8_t *)ATA_STAT;   /* write = command */
    unsigned i;

    *cmd = opcode;

    for (i = 0; i < POLL_DRQ; i++)
    {
        uint8_t st = *(volatile uint8_t *)ATA_STAT;

        last_status = st;
        if (floating(st))
            return 0;
        if ((st & ST_BSY) == 0)
        {
            if (st & ST_DRQ)
                return 1;
            if (st & ST_ERR)
                return 0;
        }
    }
    return 0;
}

/*
 * How much of a field is letters and digits.  Serves two purposes: it picks
 * which byte of each IDENTIFY word leads when decoding a string, and it is
 * the sanity check on the result as a whole -- a real model name is mostly
 * alphanumerics, while a bus nobody is driving decodes to punctuation.
 */
static unsigned text_score(const char *s, unsigned n)
{
    unsigned k = 0;

    while (n--)
    {
        unsigned char c = (unsigned char)*s++;

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z'))
            k++;
    }
    return k;
}

/*
 * IDENTIFY holds its strings two ASCII characters per 16-bit word, but which
 * byte of the word leads varies with the device.  Decode the field both ways
 * and keep the reading that actually looks like text: guessing here would
 * silently turn a model name into punctuation.
 */
static void ident_string(const uint16_t *id, unsigned count, int hi_first,
                         char *out)
{
    unsigned i;

    for (i = 0; i < count; i++)
    {
        uint16_t w = id[27 + i];

        out[i * 2]     = (char)(hi_first ? (w >> 8) & 0xff : w & 0xff);
        out[i * 2 + 1] = (char)(hi_first ? w & 0xff : (w >> 8) & 0xff);
    }
    out[count * 2] = '\0';
}

/* IDENTIFY pads strings with spaces; the log does not want them. */
static void trim(char *s)
{
    unsigned start = 0;
    unsigned n = 0;
    unsigned i;

    while (s[n])
        n++;
    while (start < n && (unsigned char)s[start] <= 0x20)
        start++;
    while (n > start && (unsigned char)s[n - 1] <= 0x20)
        n--;
    for (i = 0; i + start < n; i++)
        s[i] = s[i + start];
    s[i] = '\0';
}

static int identify_impl(unsigned unit, struct nb_ata_id *id)
{
    volatile uint16_t *data = (volatile uint16_t *)ATA_DATA;
    uint16_t ident[256];
    char hi[44];
    char lo[44];
    uint32_t sectors;
    int atapi;
    unsigned i;

    id->present = 0;
    id->atapi = 0;
    id->mb = 0;
    id->ident = 0xffff;
    id->model[0] = '\0';

    if (!select_unit(unit))
        return 0;

    /*
     * An ATAPI device announces itself by putting $14/$EB in cylinder
     * low/high and leaves zeros there when it is a disk.  That is what the
     * command word below keys off; if the device then disagrees, the other
     * IDENTIFY flavour is tried once before giving up on the unit.
     */
    atapi = (*(volatile uint8_t *)ATA_CYLL == 0x14 &&
             *(volatile uint8_t *)ATA_CYLH == 0xeb);

    if (!identify_with((uint8_t)(atapi ? CMD_IDENTIFY_PACKET
                                       : CMD_IDENTIFY)))
    {
        if (!select_unit(unit))
            return 0;
        atapi = !atapi;
        if (!identify_with((uint8_t)(atapi ? CMD_IDENTIFY_PACKET
                                           : CMD_IDENTIFY)))
            return 0;
    }

    /*
     * Gayle wires the 16-bit data register across the lanes the same way it
     * puts the eight 8-bit task file registers on odd addresses: the byte
     * that leaves first on the ATA bus comes back in the low half of the
     * m68k word, so every word has to have its halves exchanged before it
     * means anything.  Without this, word 0 reads $4000 where the device
     * wrote $0040 and the model name decodes to punctuation.
     */
    for (i = 0; i < 256; i++)
    {
        uint16_t w = *data;

        ident[i] = (uint16_t)((w << 8) | (w >> 8));
    }
    id->ident = ident[0];

    /*
     * Which IDENTIFY command succeeded is what says whether this is a disk
     * or an ATAPI device -- that is what the two commands are for.  Word 0
     * is not a contract: a fixed disk reports $0040, a CompactFlash card
     * $848A, an ATAPI device $80C0 | type, so it is used only to reject a
     * result that no device could have produced.
     */
    id->atapi = atapi;

    ident_string(ident, 20, 1, hi);
    ident_string(ident, 20, 0, lo);
    if (text_score(lo, 40) > text_score(hi, 40))
    {
        for (i = 0; i <= 40; i++)
            hi[i] = lo[i];
    }
    if (text_score(hi, 40) < 4)        /* no model name anyone could read */
        return 0;
    for (i = 0; i < 40; i++)
        id->model[i] = hi[i];
    id->model[40] = '\0';
    trim(id->model);

    if (!atapi)
    {
        /*
         * Words 60/61 are the 28-bit LBA sector count, low word first.
         * MiB is sectors / 2048 -- a shift, so the freestanding link never
         * needs a runtime divide.
         */
        sectors = (uint32_t)ident[60] | ((uint32_t)ident[61] << 16);
        id->mb = (unsigned)(sectors >> 11);
    }

    id->present = 1;
    return 1;
}

int nb_ata_identify(unsigned unit, struct nb_ata_id *id)
{
    int ok;

    last_status = BUS_FLOAT1;       /* until a read says otherwise */
    ok = identify_impl(unit, id);

    /*
     * Recorded per unit rather than globally, so a second, empty unit
     * cannot overwrite what the first one saw: the boot log reports what
     * happened on the unit it is talking about.
     */
    id->status = last_status;
    return ok;
}
