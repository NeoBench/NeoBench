/*
 * Gayle IDE driver: controller probe, disk and ATAPI CD-ROM detection,
 * and sector transfers.
 *
 * NeoBench runs bare metal with nothing under it, so the boot log has to
 * answer "is there a disk?" the way a kernel does -- by talking to the
 * hardware.  The ATA task file is programmed directly and the answer comes
 * back from IDENTIFY DEVICE or IDENTIFY PACKET DEVICE, which is what turns
 * a driver line in the log from an assertion into a measurement.
 *
 * Identification is only half of ata.device and atapi.device: once a unit
 * has answered, the same task file moves the sectors of it, which is what
 * the rest of the system reads a file through.
 *
 * Every wait in this file is bounded.  A drive that is missing, slow or
 * wedged has to answer "no device" rather than hold the boot open, and the
 * bounds are written so that the worst case costs a few tens of
 * milliseconds -- and only when something really is sitting on the bus.
 */
#include <stdint.h>
#include "amiga.h"
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
#define ATA_FEAT   0x00da0005UL
#define ATA_SECC   0x00da0009UL
#define ATA_SECN   0x00da000dUL
#define ATA_CYLL   0x00da0011UL
#define ATA_CYLH   0x00da0015UL
#define ATA_DHEAD  0x00da0019UL
#define ATA_STAT   0x00da001dUL

#define ST_BSY  0x80                 /* busy: every other bit is undefined */
#define ST_DRQ  0x08                 /* data requested: 256 words pending  */
#define ST_ERR  0x01                 /* command failed                     */

#define CMD_IDENTIFY        0xec
#define CMD_IDENTIFY_PACKET 0xa1
#define CMD_READ            0x20     /* READ SECTORS, LBA28, PIO          */
#define CMD_WRITE           0x30     /* WRITE SECTORS, LBA28, PIO         */
#define CMD_PACKET          0xa0     /* ATAPI: a command packet follows    */
#define ATAPI_READ10        0x28     /* ... of which this is READ(10)      */

#define SECTOR_BYTES   512u          /* a disk sector                      */
#define ATAPI_BYTES   2048u          /* a CD-ROM sector                    */
#define ATAPI_CDB       12u           /* the command packet, whole          */

/*
 * A bus with nobody on it floats, so the status register reads back as a
 * steady $FF.  $00 is deliberately not in this set: it is what a bus with
 * every line pulled down reads, but it is also exactly what a unit that
 * has been selected and has not come ready yet looks like, and the value
 * alone cannot tell the two apart.  Calling it empty discards the one
 * unit that was about to answer -- which is how a drive that is still
 * coming ready gets reported as "no device" -- so the probe asks instead
 * of deciding.  A bus with no drive on it never raises anything, and the
 * command that follows times out under its bound either way.
 */
#define BUS_FLOAT1  0xff
#define BUS_FLOAT2  0x7f

/*
 * Poll bounds, in bus reads.  Gayle is PIO mode 0 and its reads are slow, so
 * a few tens of thousands of reads is already tens of milliseconds;
 * IDENTIFY itself answers in well under a millisecond.
 */
#define POLL_PRESENT  40000U         /* nobody answered the selection      */
#define POLL_BSY     2000000U        /* device selected, settling          */
#define POLL_DRQ     4000000U        /* IDENTIFY result coming             */

/*
 * What a bus nobody is driving reads as, as opposed to what a device that
 * is there and not ready reads as.  The two answers are two ways of saying
 * the same nothing: $FF is every line pulled up, $7F is every line but
 * BSY -- and $7F is the one that matters, because with BSY clear, DRDY set
 * and DRQ set it is exactly the state wait_drq() is waiting for, so a
 * driver that only knows $FF will find data ready on a channel with no
 * device on it and spend 256 words of a transfer that will never come.
 * A real device cannot produce either value: bits 1 and 2 of the status
 * have been reserved since ATA-1 and $7F needs them both, and $FF needs
 * BSY and DRQ at once.
 *
 * $00 is deliberately not here.  It is a device's own answer -- nothing
 * ready, nothing claimed -- and the probe's job is to keep "never read"
 * apart from "read $00" rather than to fold one into the other.
 */
static int floating(uint8_t st)
{
    return st == BUS_FLOAT1 || st == BUS_FLOAT2;
}

/*
 * The most recent status read on the task file.  Kept per unit at the point
 * the probe finishes, so an empty second unit cannot overwrite what the
 * first one saw -- the boot log has to report the unit it talked about.
 *
 * Deliberately zero-initialised: a global carrying a non-zero initialiser
 * is emitted into .data, and rom.ld places .data in the ROM at $FC0000
 * where hardware drops the write, so the variable would never change.
 * Zero init lands it in .bss in chip RAM instead; nb_ata_identify() primes
 * it with the floating-bus value before each probe so that "never read"
 * and "read $00" stay distinguishable.
 */
static uint8_t last_status;

/* Serial helpers, defined below where the refusal line is built. */
static void s_put(const char *s);
static void s_x2(uint8_t v);

/*
 * Select a unit and wait for it to become addressable.  Returns 0 when no
 * device claimed the bus, or when the one that did never cleared BSY.
 *
 * The selection comes first, because status is the state of the selected
 * unit and not of the bus.  Before D/H has been written the register
 * still answers with whatever the last selection left there -- on a bus
 * nobody has touched since reset, that is nobody at all -- so a driver
 * that waits for an answer before asking has decided "nothing here"
 * about a device that is perfectly well here.  It is also the only way
 * round to a unit other than the one the machine booted with, which is
 * what the second unit on the bus always is: asking it for its status
 * before selecting it answers for the first one instead.
 */
static int select_unit(unsigned unit)
{
    volatile uint8_t *dh = (volatile uint8_t *)ATA_DHEAD;
    unsigned i;
    uint8_t st = BUS_FLOAT1;

    *dh = (uint8_t)(unit ? 0xb0 : 0xa0);        /* $A0 master, $B0 slave  */

    for (i = 0; i < POLL_PRESENT; i++)
    {
        st = *(volatile uint8_t *)ATA_STAT;
        last_status = st;
        if (!floating(st))
            break;
    }
    if (floating(st))
        return 0;

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
 * Wait for the device to raise DRQ after a command: 256 words are then
 * ready to move.
 *
 * DRQ may only be believed once BSY has cleared -- while BSY is set every
 * other bit is undefined, which is the classic way to get an ATA driver
 * wrong -- and an error without DRQ means the device refused the command.
 */
static int wait_drq(void)
{
    unsigned i;

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
 * Wait for the device to be neither busy nor holding data: the state a
 * command leaves behind when it has finished.  An error bit here means
 * the command failed after the data was moved, which counts as failure.
 */
static int wait_idle(void)
{
    unsigned i;

    for (i = 0; i < POLL_BSY; i++)
    {
        uint8_t st = *(volatile uint8_t *)ATA_STAT;

        last_status = st;
        if (floating(st))
            return 0;
        if ((st & ST_BSY) == 0)
            return (st & ST_ERR) ? 0 : 1;
    }
    return 0;
}

/*
 * Recover a drive that is still busy long after its data has all been
 * moved: the failure the installer actually meets, and the one where
 * doing nothing means the whole bus is dead from here on.
 *
 * The only command ATA allows into a busy drive is the one the drive
 * answers without needing to be selected first -- EXECUTE DEVICE
 * DIAGNOSTIC, which a wedged unit takes as an instruction to sort
 * itself out.  If it works, the next retry finds a drive that can be
 * selected again; if it does not, the ">ata rec" line says so and the
 * refusal stands.  Returns the status the drive showed afterwards.
 */
static void recover_busy(void)
{
    unsigned i;
    uint8_t st = BUS_FLOAT1;

    *(volatile uint8_t *)ATA_STAT = 0x90;        /* EXECUTE DIAGNOSTIC */

    for (i = 0; i < POLL_BSY; i++)
    {
        st = *(volatile uint8_t *)ATA_STAT;
        last_status = st;
        if (!floating(st) && (st & ST_BSY) == 0)
            break;
    }
    s_put(">ata rec st=$");
    s_x2(st);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}

/*
 * Move one 512 byte sector through the data register, in either
 * direction.
 *
 * The byte that leaves first on the ATA bus comes back in the HIGH half
 * of the m68k word, so a word read gives the stream's first two bytes
 * back as $5244 and they have to be taken top-down.  This is measured
 * rather than assumed: the first sector of a rigid disk starts "RDSK",
 * and taking the low byte first hands the caller "DRKS" -- close enough
 * to look plausible and wrong in a way nothing downstream would survive.
 * The two directions use the same order, so a sector written out lands
 * the bytes it was given.
 *
 * The bytes are moved one at a time rather than as words so that a
 * caller may hand in a buffer at any address: a 16-bit access through an
 * odd pointer is an address error on the 68000, and a sector buffer that
 * happens to sit one byte in from a word boundary is not an error at
 * all.
 */
static void xfer_sector(void *buf, int writing)
{
    volatile uint16_t *data = (volatile uint16_t *)ATA_DATA;
    uint8_t *p = (uint8_t *)buf;
    unsigned i;

    for (i = 0; i < SECTOR_BYTES; i += 2)
    {
        if (writing)
            *data = (uint16_t)(((unsigned)p[i] << 8) | p[i + 1]);
        else
        {
            uint16_t w = *data;

            p[i]     = (uint8_t)(w >> 8);
            p[i + 1] = (uint8_t)(w & 0xff);
        }
    }
}

/* Issue one IDENTIFY flavour and wait for its result. */
static int identify_with(uint8_t opcode)
{
    volatile uint8_t *cmd = (volatile uint8_t *)ATA_STAT;   /* write = command */

    *cmd = opcode;
    return wait_drq();
}

/*
 * How much of a field is letters and digits.  It is the sanity check on
 * the block as a whole rather than a decoder: a real model name is
 * mostly alphanumerics, while a bus nobody is driving decodes to
 * punctuation -- and it cannot be used to choose between readings,
 * because exchanging the two bytes of every word leaves the number of
 * letters in a field exactly where it was.  Word 0 answers that, and
 * this answers whether the answer was worth having.
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
 * IDENTIFY holds its strings two ASCII characters per 16-bit word, the
 * first of them in the high half -- the order the transfer and word 0
 * between them have already settled for this block.
 */
static void ident_string(const uint16_t *id, unsigned count, char *out)
{
    unsigned i;

    for (i = 0; i < count; i++)
    {
        uint16_t w = id[27 + i];

        out[i * 2]     = (char)((w >> 8) & 0xff);
        out[i * 2 + 1] = (char)(w & 0xff);
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

/*
 * A word 0 that a device could really have reported.  The standard pins
 * this one field down -- $0040 for a fixed disk, $848A for a card,
 * $80C0 | type for a packet device -- which is what makes it useful for
 * more than rejecting rubbish.
 */
static int word0_ok(unsigned v)
{
    if (v == 0x0040u)                       /* fixed disk              */
        return 1;
    if (v == 0x848au)                       /* CompactFlash            */
        return 1;
    if ((v & 0xff00u) == 0x8000u && (v & 0xc0u) == 0xc0u)
        return 1;                           /* packet device, type low */
    return 0;
}

/*
 * Turn the block over if word 0 says it arrived with each pair of bytes
 * the wrong way round, so that the rest of it is read the way the device
 * wrote it.
 *
 * The model name cannot be asked which way round it came: exchanging
 * the two bytes of a field leaves the number of letters in it exactly
 * where it was, so both readings score the same and the field that
 * looks plausible either way is no answer at all.  Word 0 is an answer
 * because it is not a name -- a block that reads $4000 came in turned
 * over, and nothing that is not a disk can have written $4000 there.
 * When word 0 says nothing either way, the block is left alone: an
 * unreadable IDENTIFY is rejected a few lines below, not repaired here.
 */
static void calibrate_word0(uint16_t *ident)
{
    unsigned v = ident[0];
    unsigned i;

    if (word0_ok(v))
        return;
    v = (v >> 8) | (v << 8);
    if (!word0_ok(v & 0xffffu))
        return;
    for (i = 0; i < 256; i++)
    {
        unsigned w = ident[i];

        ident[i] = (uint16_t)((w << 8) | (w >> 8));
    }
}

static int identify_impl(unsigned unit, struct nb_ata_id *id)
{
    uint16_t ident[256];
    char hi[44];
    uint32_t sectors;
    int atapi;
    unsigned i;
    unsigned sig_lo, sig_hi;

    id->present = 0;
    id->atapi = 0;
    id->mb = 0;
    id->ident = 0xffff;
    /*
     * The signature is seeded with the same $FFFF, and for the same
     * reason ident is: a unit nobody answers selection for returns
     * before the cylinder registers are read at all now, and $FFFF is
     * what an unanswered bus reads in them, so the boot log's line says
     * the same thing whichever of the two paths produced it -- that
     * there is nothing on the bus.
     */
    id->sig = 0xffff;
    id->model[0] = '\0';

    if (!select_unit(unit))
        return 0;

    /*
     * An ATAPI device announces itself by putting $14/$EB in cylinder
     * low/high and leaves zeros there when it is a disk.  That is what the
     * command word below keys off; if the device then disagrees, the other
     * IDENTIFY flavour is tried once before giving up on the unit.
     *
     * The two bytes are kept either way, because they are the only thing
     * a packet device offers that does not depend on its agreeing to
     * IDENTIFY: a drive that answers with an error for both flavours has
     * still said which kind of drive it is.
     */
    sig_lo = *(volatile uint8_t *)ATA_CYLL;
    sig_hi = *(volatile uint8_t *)ATA_CYLH;
    id->sig = ((unsigned)sig_lo << 8) | sig_hi;
    atapi = (sig_lo == 0x14 && sig_hi == 0xeb);

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
     * The transfer is the job of the routine above; what matters here is
     * that 256 words of IDENTIFY end up in the buffer the way the
     * device wrote them, whichever way the bus handed them over.
     */
    xfer_sector(ident, 0);
    calibrate_word0(ident);
    id->ident = ident[0];

    /*
     * Which IDENTIFY command succeeded is what says whether this is a disk
     * or an ATAPI device -- that is what the two commands are for.  Word 0
     * has already had its say about how the block was read; what is kept
     * of it here is the one bit the boot log still asks for, the one that
     * tells a CompactFlash card from a fixed disk.
     */
    id->atapi = atapi;

    ident_string(ident, 20, hi);
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

/*
 * ---------------------------------------------------------------------------
 * Sector I/O -- NeoBench's own ata.device and atapi.device.
 * ---------------------------------------------------------------------------
 *
 * The probe above answers "is there a disk?"; these answer "give me a
 * sector of it", which is what turns the two into drivers rather than
 * into a pair of lines in the boot log.  Everything is PIO mode 0: Gayle
 * has no DMA worth the name and the transfers are short, so the task file
 * is programmed, DRQ is polled under a bound, and the words come off the
 * data register one sector at a time.
 *
 * Both directions are implemented for a disk -- NeoBench downloads into
 * Temp/ and will want to write back -- while a CD-ROM is read only,
 * which the device table records by leaving `write' null rather than by
 * failing a write later.
 *
 * Every path returns 1 only when every sector moved and the device
 * finished without an error bit.  A missing drive, a unit that is really
 * a CD, a transfer past the end of LBA28 or a device that refuses the
 * command all answer 0, and none of them waits forever.
 */

/*
 * The one line this driver writes for itself: a refused transfer, with
 * the phase that refused it and the status byte the drive was holding.
 * Everything else the log says about ata.device comes from someone
 * asking; this one exists because "the transfer stopped" with no phase
 * and no status leaves the reader to guess between a drive that said
 * no (ERR set), a drive that went quiet (poll bound reached) and a bus
 * that floated ($FF) -- three different faults with one symptom, and
 * the installer's "op=hd" only says which end of the cable it was.
 */
static void s_put(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
}

static void s_u(unsigned v)
{
    char b[11];
    int i = 11;

    b[--i] = '\0';
    do
    {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    s_put(&b[i]);
}

static void s_x2(uint8_t v)
{
    static const char hex[] = "0123456789abcdef";
    char b[3];

    b[0] = hex[(v >> 4) & 0xf];
    b[1] = hex[v & 0xf];
    b[2] = '\0';
    s_put(b);
}

static int rw_refused(const char *phase, uint32_t lba, unsigned s,
                      int writing)
{
    s_put(">ata err phase=");
    s_put(phase);
    s_put(" st=$");
    s_x2(last_status);
    s_put(writing ? " w=1 lba=" : " w=0 lba=");
    s_u((unsigned)lba);
    s_put(" s=");
    s_u(s);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
    return 0;
}

static int rw_sectors(unsigned unit, uint32_t lba, unsigned count,
                      void *buf, int writing)
{
    uint8_t *p = (uint8_t *)buf;
    volatile uint8_t *cmd = (volatile uint8_t *)ATA_STAT;   /* write = command */
    unsigned s;

    if (!buf || count == 0 || count > 255u)
        return 0;
    if (lba >= 0x10000000ul || (uint32_t)count > 0x10000000ul - lba)
        return 0;                       /* LBA28 only; never wrap an address */

    if (!select_unit(unit))
    {
        int r = rw_refused("sel", lba, 0, writing);

        recover_busy();             /* the refusal line comes first   */
        return r;
    }

    *(volatile uint8_t *)ATA_FEAT  = 0;
    *(volatile uint8_t *)ATA_SECC  = (uint8_t)count;
    *(volatile uint8_t *)ATA_SECN  = (uint8_t)lba;
    *(volatile uint8_t *)ATA_CYLL  = (uint8_t)(lba >> 8);
    *(volatile uint8_t *)ATA_CYLH  = (uint8_t)(lba >> 16);
    /*
     * Head/sector: LBA mode, the drive bit, then the top four LBA bits.
     * The drive bit has to come from the unit and not from the address,
     * or unit 1 would quietly read unit 0's sectors for any LBA below
     * $1000000.
     */
    *(volatile uint8_t *)ATA_DHEAD = (uint8_t)(0xe0u | (unit ? 0x10u : 0u) |
                                               ((lba >> 24) & 0x0fu));
    *cmd = writing ? CMD_WRITE : CMD_READ;

    for (s = 0; s < count; s++)
    {
        if (!wait_drq())
            return rw_refused("drq", lba, s, writing);
        xfer_sector(p, writing);
        p += SECTOR_BYTES;
    }
    /*
     * The loop above stops only once every sector has gone over, so a
     * device still reading busy here has not lost the data: it has lost
     * the end of its own command.  Nothing is left to send, which is
     * what separates this from a device that is merely slow, and
     * waiting longer will not send a completion nobody sent.  So the
     * line is written with the status that failed, the unit is given
     * what unsticks it, and the caller gets a refusal against a bus
     * that can be selected again -- which is what install.c's three
     * attempts to a chunk are for, and what the read-back after every
     * chunk is there to judge.
     */
    if (!wait_idle())
    {
        int r = rw_refused("idle", lba, s, writing);

        recover_busy();
        return r;
    }
    return 1;
}

int nb_ata_read(unsigned unit, uint32_t lba, void *dst, unsigned count)
{
    return rw_sectors(unit, lba, count, dst, 0);
}

int nb_ata_write(unsigned unit, uint32_t lba, const void *src, unsigned count)
{
    return rw_sectors(unit, lba, count, (void *)src, 1);
}

/*
 * A packet command that refused, named the same way as the disk's:
 * the line first (so the status shown is the one that failed), then
 * whatever EXECUTE DEVICE DIAGNOSTIC can do about it.  The CD takes
 * the same BSY wedge as the disk does -- the bus is shared and the
 * same emulation drives both -- and without this the installer's
 * retry would find nothing selectable where it had left it.
 */
static int atapi_refused(const char *phase, uint32_t lba, unsigned s)
{
    int r = rw_refused(phase, lba, s, 0);

    recover_busy();
    return r;
}

/*
 * READ(10) through the ATAPI packet interface.
 *
 * Two phases make this different from a disk: the command is handed over
 * as a twelve byte packet through the same data register, and the answer
 * arrives in whole CD sectors of 2048 bytes.
 *
 * The byte transfer count registers (cylinder low/high) are what make
 * the answer whole.  They are written with $FFFF before PACKET, and the
 * device is free to read them at that moment to decide how the data
 * comes back: left over from a disk command they hold part of an
 * address -- a few hundred -- and the sector then arrives in pieces of
 * that many bytes, each piece separated by busy, which a transfer reading
 * ahead blind walks straight into.  $FFFF asks for the sector in one
 * piece, which is what every ATAPI driver asks for.
 *
 * The packet is sent as the twelve bytes it is.  Once the device has
 * counted its own command size it starts the transfer, and a word written
 * after that point lands in the data instead of the command.
 *
 * The transfer is read in sector-sized blocks, each one preceded by a
 * wait for DRQ: a device that answers a multi-sector read in one block
 * keeps DRQ raised through it, and one that answers sector by sector
 * raises it again for the next, so both come out the same way here.
 */
static int atapi_read_sectors(unsigned unit, uint32_t lba, unsigned count,
                              void *buf)
{
    uint8_t *p = (uint8_t *)buf;
    volatile uint16_t *data = (volatile uint16_t *)ATA_DATA;
    uint8_t cdb[20];
    unsigned s;
    unsigned i;

    if (!buf || count == 0 || count > 0xffffu)
        return 0;
    if (!select_unit(unit))
        return atapi_refused("asel", lba, 0);

    for (i = 0; i < sizeof cdb; i++)
        cdb[i] = 0;
    cdb[0] = (uint8_t)ATAPI_READ10;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)lba;
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)count;

    /*
     * Byte transfer count: $FFFF asks for each sector whole.  Written
     * here, immediately before PACKET, because the device reads the
     * registers at the command itself -- a disk command's address left
     * in them would ask for the answer in that many byte pieces.
     */
    *(volatile uint8_t *)ATA_CYLL = 0xff;
    *(volatile uint8_t *)ATA_CYLH = 0xff;
    *(volatile uint8_t *)ATA_FEAT = 0;
    *(volatile uint8_t *)ATA_STAT = CMD_PACKET;

    if (!wait_drq())
        return atapi_refused("apkt", lba, 0);
    for (i = 0; i < ATAPI_CDB; i += 2)
        *data = (uint16_t)(((unsigned)cdb[i] << 8) | cdb[i + 1]);

    for (s = 0; s < count; s++)
    {
        if (!wait_drq())
            return atapi_refused("asec", lba, s);
        for (i = 0; i < ATAPI_BYTES; i += 2)
        {
            uint16_t w = *data;

            p[i]     = (uint8_t)(w >> 8);
            p[i + 1] = (uint8_t)(w & 0xff);
        }
        p += ATAPI_BYTES;
    }
    if (!wait_idle())
        return atapi_refused("aidle", lba, count);
    return 1;
}

int nb_atapi_read(unsigned unit, uint32_t lba, void *dst, unsigned count)
{
    return atapi_read_sectors(unit, lba, count, dst);
}

/*
 * The task file status as the last operation left it -- the register a
 * refused command parks its reason in, reported so a failure can say
 * what the device said rather than only that it failed.
 */
unsigned nb_ata_status(void)
{
    return last_status;
}
