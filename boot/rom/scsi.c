/*
 * A4000T SCSI host: the NCR53C710 SCRIPTS engine in the Gayle window.
 *
 * This is the one bus an AGA machine can carry without any expansion at
 * all: the A4000T motherboard puts an NCR53C710 at $DD0000 and no other
 * machine in the family has one.  So the driver looks for the chip and
 * not for the machine -- the identification is the chip's own CTEST1
 * register, which answers $F0 out of reset, plus a scratch register
 * round trip that nothing else in the window can pass -- and an A1200
 * that finds nothing has a bus with no host on it, which is what it
 * reports.
 *
 * Everything past identification is SCRIPTS: the chip is handed a short
 * program in RAM that selects a target, sends the identify message,
 * runs the command, moves whatever data the command names, and takes
 * status and the message that ends it.  A program is built per command
 * because the phases it names and the address the data moves to are
 * both this command's, and because the chip fetches its instructions by
 * address while this ROM is not writable -- so the program lives in
 * .bss, which is.
 *
 * The chip is polled rather than wired to an interrupt NeoBench owns:
 * SIEN and DIEN stay clear, which holds the interrupt line low while
 * ISTAT still reports that the program has stopped.  Every wait is
 * bounded, and a refusal is written to the serial line the way ata.c
 * writes one, so a transfer that stalls cannot also be silent about it.
 */
#include <stdint.h>
#include "amiga.h"
#include "scsi.h"

/*
 * Where the chip is, and how a register reaches it.
 *
 * The board hands the NCR its bytes in the other order from a 68k
 * longword, so the register numbered R reads at window + (R xor 3) --
 * which is what puts CTEST1 at $DD0056.  Only two windows can decode it
 * at all, because the byte has to sit at $40 or above for the board to
 * look there: the primary bank at $40 and the alternate at $80, which
 * decode the same registers.
 */
#define NCR_MEM  0x00dd0000ul
#define NCR_WIN0 0x40ul
#define NCR_WIN1 0x80ul

/* register numbers, as the chip numbers them */
#define R_SCNTL0 0x00
#define R_SCNTL1 0x01
#define R_SIEN   0x03
#define R_SCID   0x04
#define R_SXFER  0x05
#define R_ISTAT  0x21
#define R_DSTAT  0x0c
#define R_SSTAT0 0x0d
#define R_SSTAT2 0x0f
#define R_CTEST1 0x15
#define R_DSP    0x2c
#define R_SCR    0x34
#define R_DMODE  0x38
#define R_DIEN   0x39
#define R_DWT    0x3a
#define R_DCNTL  0x3b

#define CTEST1_RESET 0xf0     /* what the chip answers before it is touched */
#define SCR_EVEN 0x5a         /* the two halves of a scratch round trip     */
#define SCR_ODD  0xa5

#define ISTAT_DIP  0x01
#define ISTAT_SIP  0x02
#define ISTAT_ABRT 0x80
#define DSTAT_SIR  0x04

/*
 * The interrupt line is not ours, so completion is a bounded poll of
 * ISTAT.  The bound is the one ata.c allows a drive that never raises
 * DRQ: long enough for one that is still spinning up, and a stop rather
 * than an open end for one that will never answer at all.
 */
#define POLL_DONE 4000000U

/*
 * SCRIPTS instruction encodings.  The chip reads a program as natural
 * 68k big-endian words: a block move carries its phase in bits 24..26
 * and its count below, a select carries the target as a bit mask in
 * bits 16..23 with bit 24 asking for ATN, and the transfer-control word
 * that ends every program has bits 31/30 selecting the instruction
 * class, bits 27..25 the interrupt opcode and bit 19 the jump-if-true
 * that makes it unconditional rather than a no-op.
 */
#define SCN_MOVE(phase, n) (((uint32_t)(phase) << 24) | (uint32_t)(n))
#define SCN_SELECT(t, atn) (0x40000000ul | ((uint32_t)(atn) << 24) | \
                            ((uint32_t)1 << (16 + (t))))
#define SCN_STOP           0x98080000ul

/* the phases a program names */
#define P_DI 1                 /* data in:  the target writes, we read    */
#define P_DO 0                 /* data out: the target reads, we write    */
#define P_CMD 2
#define P_ST 3
#define P_MO 6
#define P_MI 7

/* max sectors one program can move: the count field is 24 bits wide */
#define SECTORS_MAX 32767u

/* the window that answered, and what it said */
static unsigned scsi_win;
static int      scsi_found;
static unsigned scsi_status;
static unsigned scsi_base;
static unsigned scsi_units;
static uint32_t scsi_last[8];        /* last LBA each target has to give */

/* where the last program stopped, for the refusal line */
static const char *last_phase;
static unsigned    last_dstat;
static unsigned    last_sstat0;
static unsigned    last_sstat2;
static unsigned    last_dsp;

/* the program and the words it points at: RAM, because the chip fetches */
static uint32_t scn[20];
static unsigned scn_start;
static unsigned scn_steps;
static const char *scn_phase[10];
static uint8_t  scn_msg;
static uint8_t  scn_cdb[16];
static uint8_t  scn_stat;
static uint8_t  scn_msgin;
static uint8_t  scn_sense[18];
static uint8_t  scn_inq[36];
static uint8_t  scn_cap[8];

static volatile uint8_t *reg_at(unsigned win, unsigned reg)
{
    return (volatile uint8_t *)(NCR_MEM + win + (reg ^ 3u));
}

#define REG(r) (*reg_at(scsi_win, (r)))

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

static void s_x8(unsigned v)
{
    static const char hex[] = "0123456789abcdef";
    char b[9];
    int i;

    for (i = 0; i < 8; i++)
        b[i] = hex[(v >> ((7 - i) * 4)) & 0xf];
    b[8] = '\0';
    s_put(b);
}

/*
 * The six phases a SCSI bus can be in, named the way the chip's own
 * documentation names them.  A refusal says which instruction was being
 * attempted and which phase the bus was actually in when it stopped --
 * two facts that are only ever the same when nothing is wrong, so
 * printing both is what turns "it failed" into "it failed here".
 */
static const char *phase_name(unsigned v)
{
    switch (v & 7u)
    {
    case 0:  return "do";
    case 1:  return "di";
    case 2:  return "cmd";
    case 3:  return "st";
    case 6:  return "mo";
    case 7:  return "mi";
    default: return "?";
    }
}

/*
 * One line saying where a command stopped and what the chip reported
 * when it did.  The phase is the instruction the program was at, so the
 * line locates the refusal inside the sequence rather than only inside
 * the driver, and dsp is the address that instruction lives at.
 */
static int refuse(unsigned t, uint32_t lba, unsigned s)
{
    s_put(">scsi err phase=");
    s_put(last_phase);
    s_put(" got=");
    s_put(phase_name(last_sstat2));
    s_put(" t=");
    s_u(t);
    s_put(" st=$");
    s_x2((uint8_t)scsi_status);
    s_put(" d=$");
    s_x2((uint8_t)last_dstat);
    s_put(" s0=$");
    s_x2((uint8_t)last_sstat0);
    s_put(" dsp=$");
    s_x8(last_dsp);
    s_put(" lba=");
    s_u((unsigned)lba);
    s_put(" s=");
    s_u(s);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
    return 0;
}

/*
 * The scratch register has to give back what it was given, twice, with
 * two different values: a window that merely reads back whatever was
 * written cannot pass both, and one that cannot be written at all fails
 * the first.  Nothing outside the chip answers this way.
 */
static int scratch_roundtrip(void)
{
    volatile uint8_t *r = reg_at(scsi_win, R_SCR);

    *r = SCR_EVEN;
    if (*r != SCR_EVEN)
        return 0;
    *r = SCR_ODD;
    return *r == SCR_ODD;
}

/*
 * Take the chip to a known state before anything is asked of it.
 *
 * SCNTL1 is written without the bits that reset the bus immediately,
 * since that is not wanted here; SIEN and DIEN stay clear so completion
 * is this driver's poll and not an interrupt nobody in this system
 * services; and DMODE clears the manual bit, so loading DSP is what
 * starts a program.  DSTAT and SSTAT0 are read rather than written
 * because both clear on read -- that is the only way to be sure a stale
 * failure is not about to be read as some later command's.
 */
static void chip_init(void)
{
    REG(R_ISTAT) = 0;
    REG(R_SCNTL0) = 0xc0;         /* arbitrate, but never start by writing */
    REG(R_SCNTL1) = 0x00;
    REG(R_SCID) = 0x80;           /* this adapter is SCSI ID 7             */
    REG(R_SXFER) = 0x00;
    REG(R_SIEN) = 0x00;
    REG(R_DIEN) = 0x00;
    REG(R_DMODE) = 0x00;
    REG(R_DWT) = 0x00;
    REG(R_DCNTL) = 0x00;
    (void)REG(R_DSTAT);
    (void)REG(R_SSTAT0);
    REG(R_ISTAT) = 0;
}

static unsigned read_dsp(void)
{
    unsigned v;

    v = REG(R_DSP + 3);
    v = (v << 8) | REG(R_DSP + 2);
    v = (v << 8) | REG(R_DSP + 1);
    v = (v << 8) | REG(R_DSP + 0);
    return v;
}

/*
 * Run one command on one target.
 *
 * The program is select-with-ATN, the identify byte, the command, any
 * data the command names, one byte of status, and the message in that
 * ends the exchange.  A command with no data leaves the data move out
 * altogether rather than asking for zero bytes, which would be a
 * different instruction with a different way of going wrong.
 *
 * Returns 1 when the program reached its interrupt with nothing the
 * chip counts as wrong, and 0 otherwise; in both cases last_* say where
 * it got to, so the refusal line is the same line for a target that is
 * not there and for a program that stopped in the middle.
 */
static int xact(unsigned t, const uint8_t *cmd, unsigned cmdlen,
                const void *data, unsigned bytes, int dir)
{
    unsigned i = 0;
    unsigned k;
    unsigned idx;
    int timed_out;

    if (!scsi_found || t > 7 || cmdlen < 6 || cmdlen > 16)
        return 0;
    if (bytes && !data)
        return 0;

    for (k = 0; k < cmdlen; k++)
        scn_cdb[k] = cmd[k];

    scn_msg = 0x80;                /* identify: this adapter is a host    */
    scn_stat = 0xff;               /* poison: no status came back at all  */
    scn_msgin = 0xff;
    last_phase = "boot";

    scn_phase[i / 2 + 1] = "sel";
    scn[i++] = SCN_SELECT(t, 1);
    scn[i++] = 0;                  /* filled in once the stop is known    */

    scn_phase[i / 2 + 1] = "msg";
    scn[i++] = SCN_MOVE(P_MO, 1);
    scn[i++] = (uint32_t)(uintptr_t)&scn_msg;

    scn_phase[i / 2 + 1] = "cmd";
    scn[i++] = SCN_MOVE(P_CMD, cmdlen);
    scn[i++] = (uint32_t)(uintptr_t)scn_cdb;

    if (bytes)
    {
        scn_phase[i / 2 + 1] = "data";
        scn[i++] = SCN_MOVE(dir < 0 ? P_DI : P_DO, bytes);
        scn[i++] = (uint32_t)(uintptr_t)data;
    }

    scn_phase[i / 2 + 1] = "stat";
    scn[i++] = SCN_MOVE(P_ST, 1);
    scn[i++] = (uint32_t)(uintptr_t)&scn_stat;

    scn_phase[i / 2 + 1] = "msgin";
    scn[i++] = SCN_MOVE(P_MI, 1);
    scn[i++] = (uint32_t)(uintptr_t)&scn_msgin;

    scn_steps = i / 2;
    scn[i] = SCN_STOP;
    scn[i + 1] = 0;
    scn_phase[scn_steps + 1] = "done";
    scn[1] = (uint32_t)(uintptr_t)(scn + i);   /* where SELECT goes if it
                                                   finds the bus is ours  */
    scn_start = (unsigned)(uintptr_t)scn;

    /* drop what a previous program left, then load this one.  The high
       byte of DSP goes last: writing it is what starts the chip.        */
    (void)REG(R_DSTAT);
    (void)REG(R_SSTAT0);
    REG(R_DSP + 0) = (uint8_t)(scn_start);
    REG(R_DSP + 1) = (uint8_t)(scn_start >> 8);
    REG(R_DSP + 2) = (uint8_t)(scn_start >> 16);
    REG(R_DSP + 3) = (uint8_t)(scn_start >> 24);

    for (k = 0; k < POLL_DONE; k++)
        if (REG(R_ISTAT) & (ISTAT_DIP | ISTAT_SIP))
            break;
    timed_out = (k == POLL_DONE);
    if (timed_out)
    {
        REG(R_ISTAT) = ISTAT_ABRT; /* stop whatever is still going        */
        REG(R_ISTAT) = 0;
    }

    last_dstat = REG(R_DSTAT);
    last_sstat0 = REG(R_SSTAT0);
    last_sstat2 = REG(R_SSTAT2);
    last_dsp = read_dsp();
    scsi_status = scn_stat;

    idx = 0;
    if (last_dsp >= scn_start)
        idx = (last_dsp - scn_start) / 8;
    if (idx > scn_steps + 1)
        idx = 0;
    last_phase = (idx && scn_phase[idx]) ? scn_phase[idx] : "poll";

    if (timed_out)
        return 0;
    if ((last_dstat & DSTAT_SIR) && last_sstat0 == 0)
        return 1;
    return 0;
}

/*
 * Is there a target at this number, and is it ready to be talked to?
 *
 * The question is TEST UNIT READY, which says "are you there" by being
 * answerable at all.  A target that answers with a check condition has
 * not failed that question -- it has a condition pending from whatever
 * happened before it, which is what REQUEST SENSE exists to read away --
 * so that is asked once and the question asked again, once.
 */
static int target_ready(unsigned t)
{
    uint8_t c[6];
    unsigned i;

    for (i = 0; i < 6; i++)
        c[i] = 0;
    if (!xact(t, c, 6, 0, 0, 0))
        return 0;
    if (scsi_status == 0)
        return 1;

    c[0] = 0x03;                   /* REQUEST SENSE (Amiga numbering)     */
    c[4] = (uint8_t)sizeof scn_sense;
    if (!xact(t, c, 6, scn_sense, sizeof scn_sense, -1))
        return 0;

    for (i = 0; i < 6; i++)
        c[i] = 0;
    if (!xact(t, c, 6, 0, 0, 0))
        return 0;
    return scsi_status == 0;
}

/*
 * How big is the target: READ CAPACITY, which is the one answer sector
 * I/O stands on.  Without it, a transfer past the end of the disk could
 * only be discovered by running it, and a program that runs a command
 * the target refuses leaves the exchange somewhere other than where the
 * next one expects to find it.
 */
static int target_size(unsigned t, uint32_t *last, unsigned *mb)
{
    uint8_t c[10];
    unsigned i;
    uint32_t n;

    for (i = 0; i < 10; i++)
        c[i] = 0;
    c[0] = 0x25;                   /* READ CAPACITY(10)                  */
    if (!xact(t, c, 10, scn_cap, sizeof scn_cap, -1))
        return 0;
    if (scsi_status != 0)
        return 0;

    n = ((uint32_t)scn_cap[0] << 24) | ((uint32_t)scn_cap[1] << 16) |
        ((uint32_t)scn_cap[2] << 8) | (uint32_t)scn_cap[3];
    *last = n;
    *mb = (unsigned)((n + 1u) >> 11);      /* sectors -> MiB              */
    return 1;
}

/*
 * What the target calls itself, from INQUIRY.  The reply carries eight
 * bytes of vendor and sixteen of product, both padded with spaces
 * rather than ended, so the run between the first and last printable
 * character is what is kept -- and an answer with nothing in it stays
 * empty for the caller to name some other way.
 */
static void target_model(unsigned t, char *out, unsigned outlen)
{
    uint8_t c[6];
    char tmp[24];
    unsigned i;
    unsigned n = 0;
    unsigned start;
    unsigned end;

    for (i = 0; i < 6; i++)
        c[i] = 0;
    c[0] = 0x12;                   /* INQUIRY                            */
    c[4] = (uint8_t)sizeof scn_inq;
    if (!xact(t, c, 6, scn_inq, sizeof scn_inq, -1) || scsi_status != 0)
        return;

    for (i = 8; i < 32 && n < sizeof tmp; i++)
    {
        uint8_t ch = scn_inq[i];

        tmp[n++] = (ch >= 0x20 && ch <= 0x7e) ? (char)ch : ' ';
    }
    start = 0;
    while (start < n && tmp[start] == ' ')
        start++;
    end = n;
    while (end > start && tmp[end - 1] == ' ')
        end--;

    n = end - start;
    if (n > outlen - 1)
        n = outlen - 1;
    for (i = 0; i < n; i++)
        out[i] = tmp[start + i];
    out[n] = '\0';
}

const struct nb_scsi_info *nb_scsi_probe(void)
{
    static struct nb_scsi_info info;
    unsigned k;
    unsigned t;

    info.found = 0;
    info.win = 0;
    info.base = 0;
    info.units = 0;
    info.mb = 0;
    info.model[0] = '\0';

    scsi_found = 0;
    scsi_win = 0;
    scsi_status = 0;
    scsi_base = 0;
    scsi_units = 0;
    last_phase = "probe";
    last_dstat = 0;
    last_sstat0 = 0;
    last_sstat2 = 0;
    last_dsp = 0;
    for (k = 0; k < 8; k++)
        scsi_last[k] = 0;

    /*
     * Identification first, and only then a write: CTEST1 answers $F0
     * out of reset and nothing else in the window does, so no machine
     * without this chip ever has a byte poked at it on the strength of a
     * guess.  The scratch round trip is what makes the answer an
     * identification rather than a coincidence.
     */
    for (k = 0; k < 2; k++)
    {
        unsigned w = (k == 0) ? NCR_WIN0 : NCR_WIN1;

        if (*reg_at(w, R_CTEST1) != CTEST1_RESET)
            continue;
        scsi_win = w;
        if (!scratch_roundtrip())
        {
            scsi_win = 0;
            continue;
        }
        scsi_found = 1;
        info.found = 1;
        info.win = w;
        break;
    }
    if (!scsi_found)
        return &info;

    chip_init();

    /*
     * Targets are asked in order and the run that answers is the run
     * that gets bound: a bus with a disk on 0 and another on 3 has one
     * contiguous device and one gap, and the gap is the end of the
     * answer rather than an invitation to invent a unit number for the
     * second disk.  Capacity comes before the model because capacity is
     * what I/O stands on, and a target that cannot say how big it is is
     * a target this driver will not move sectors through.
     */
    t = 0;
    while (t < 8)
    {
        unsigned mb = 0;

        if (!target_ready(t))
        {
            if (scsi_units)
                break;
            t++;
            continue;
        }
        if (!target_size(t, &scsi_last[t], &mb))
        {
            if (scsi_units)
                break;
            refuse(t, 0, 0);       /* it answered, then would not say     */
            t++;
            continue;
        }
        if (scsi_units == 0)
        {
            scsi_base = t;
            info.base = t;
            info.mb = mb;
            target_model(t, info.model, sizeof info.model);
        }
        scsi_units++;
        info.units++;
        t++;
    }

    return &info;
}

/*
 * Move sectors.  Both directions come through here because the only
 * difference between them is which way the data phase runs, and the
 * refusal line wants to say the same thing about either.
 */
/*
 * READ(10)/WRITE(10): opcode, flags, a 32 bit address in bytes 2..5, and
 * the transfer length in bytes 7..8 with byte 9 left as control.
 *
 * The length is the one field that cannot be guessed at -- a zero there
 * is a legal command asking for nothing, and a legal command asking for
 * nothing completes the exchange without moving the data the program
 * below is waiting to move.  It sits at bytes 7..8 and nowhere else, so
 * tools/tests/test_scsi.c can hold the whole layout to the SCSI-2
 * wording without a bus in sight.
 */
static void rw_cdb(uint8_t *c, uint32_t lba, unsigned count, int writing)
{
    unsigned i;

    for (i = 0; i < 10; i++)
        c[i] = 0;
    c[0] = (uint8_t)(writing ? 0x2a : 0x28); /* WRITE(10) / READ(10)    */
    c[2] = (uint8_t)(lba >> 24);
    c[3] = (uint8_t)(lba >> 16);
    c[4] = (uint8_t)(lba >> 8);
    c[5] = (uint8_t)lba;
    c[7] = (uint8_t)(count >> 8);
    c[8] = (uint8_t)count;
}

static int rw(unsigned unit, uint32_t lba, const void *buf, unsigned count,
              int writing)
{
    uint8_t c[10];
    unsigned bytes;

    if (!buf || count == 0 || count > SECTORS_MAX)
        return 0;
    if (!scsi_found || unit < scsi_base || unit - scsi_base >= scsi_units)
        return 0;
    if (lba > scsi_last[unit] ||
        (uint32_t)(count - 1) > scsi_last[unit] - lba)
        return 0;                  /* past the end: the device says so    */

    bytes = count * 512u;
    rw_cdb(c, lba, count, writing);

    if (!xact(unit, c, 10, buf, bytes, writing ? 1 : -1))
        return refuse(unit, lba, 0);
    if (scsi_status != 0)
        return refuse(unit, lba, 0);
    return 1;
}

int nb_scsi_read(unsigned unit, uint32_t lba, void *dst, unsigned count)
{
    return rw(unit, lba, dst, count, 0);
}

int nb_scsi_write(unsigned unit, uint32_t lba, const void *src,
                  unsigned count)
{
    return rw(unit, lba, src, count, 1);
}

unsigned nb_scsi_status(void)
{
    return scsi_status;
}
