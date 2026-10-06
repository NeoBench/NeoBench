#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/ata.h"
#include "../../boot/rom/dev.h"
#include "../../boot/rom/install.h"
#include "../../boot/rom/iso9660.h"
#include "../../boot/rom/probe.h"
#include "../../boot/rom/scsi.h"
#include "../../boot/rom/usb.h"
#include "../../boot/rom/zz9000.h"

/*
 * systemd-style boot log.
 *
 * No banner art, no rule lines: one header line, then a queue of units.
 * Each unit is announced with a dim `Starting <unit>...` line in the
 * message column and answered with `[  OK  ] Started <unit>.` when it is
 * done, so the tag column never moves and a missing feature is visible
 * by colour alone without re-reading the text.  The units are NeoBench's
 * own -- what this ROM actually does on this machine -- not borrowed
 * names for work that is not happening here.
 */

/* Hardware probes: boot/rom/probe.c + boot/rom/fline.S. */
extern int  nb_probe_rtg(void);
extern int  nb_probe_cpu(void);     /* 20/30/40/60 for 68020..68060 */
extern void nb_fpu_enable_060(void); /* PCR: switch the 68060 FPU on */
extern unsigned nb_probe_fpu(void);
extern unsigned nb_probe_mmu(void);

static void status_line(const char *tag, unsigned color, const char *msg)
{
    console_write("[");
    console_set_color(color);
    console_write(tag);
    console_write("] ");
    console_set_color(NB_COL_WHITE);
    console_write(msg);
    console_write("\n");
    console_set_color(NB_COL_GREEN);
}

void kernel_ok(const char *msg)
{
    status_line("  OK  ", NB_COL_GREEN, msg);
}

void kernel_fail(const char *msg)
{
    status_line("FAILED", NB_COL_RED, msg);
}

void kernel_warn(const char *msg)
{
    status_line(" WARN ", NB_COL_AMBER, msg);
}

/* Message composition without a printf -- and without libgcc: the
 * freestanding link has no __udivsi3, so no division by a runtime
 * value, not even for the decimal digits. */
/* Both composers leave a terminator behind them and return the address of
 * it, so a chain of writes is never handed to kernel_ok() unterminated and
 * the next write simply overwrites the terminator on its way through. */
static char *put_str(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = '\0';
    return d;
}

static char *put_num(char *d, unsigned v)
{
    static const unsigned decade[10] = {
        1000000000U, 100000000U, 10000000U, 1000000U, 100000U,
        10000U, 1000U, 100U, 10U, 1U
    };
    unsigned i;
    int started = 0;

    for (i = 0; i < 10; i++)
    {
        unsigned digit = 0;

        while (v >= decade[i])
        {
            v -= decade[i];
            digit++;
        }
        if (digit || started || i == 9)
        {
            started = 1;
            *d++ = (char)('0' + digit);
        }
    }
    *d = '\0';
    return d;
}

/*
 * Exactly `digits` hexadecimal digits, for addresses and version words
 * where the width carries the meaning: a base address is eight digits
 * because it is a long, a firmware version is four because it is a word.
 */
static char *put_hex(char *d, unsigned v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < digits; i++)
        d[i] = hex[(v >> ((digits - 1 - i) * 4)) & 15u];
    d[digits] = '\0';
    return d + digits;
}

/*
 * `status $xx, identify $xxxx`: what the bus answered when it was asked
 * and gave no device back.  $FF is a bus nobody is driving, $00 a bus
 * every line is pulled down, and an identify word of $FFFF means no
 * result was offered at all -- so this is the difference between a line
 * that says "not here" and one that says why.  Both device lines report
 * it the same way, so a failure reads the same whichever unit it came
 * from.  The caller adds the brackets.
 */
static char *put_st_id(char *d, unsigned st, unsigned idv)
{
    static const char hex[] = "0123456789ABCDEF";
    int nibble;

    d = put_str(d, "status $");
    *d++ = hex[(st >> 4) & 0xf];
    *d++ = hex[st & 0xf];
    d = put_str(d, ", identify $");
    for (nibble = 12; nibble >= 0; nibble -= 4)
        *d++ = hex[(idv >> nibble) & 0xf];
    *d = '\0';
    return d;
}

/*
 * `signature $14/$EB`: the two cylinder registers as the unit left them
 * on selection.  A packet device marks itself there whatever else it
 * does -- including refusing IDENTIFY outright -- so this is what tells
 * a drive that will not be questioned apart from a bus with no drive.
 */
static char *put_sig(char *d, unsigned sig)
{
    static const char hex[] = "0123456789ABCDEF";

    d = put_str(d, "signature $");
    *d++ = hex[(sig >> 12) & 0xf];
    *d++ = hex[(sig >> 8) & 0xf];
    d = put_str(d, "/");
    *d++ = hex[(sig >> 4) & 0xf];
    *d++ = hex[sig & 0xf];
    *d = '\0';
    return d;
}

/*
 * The two shapes systemd writes a boot log in, in NeoBench's own units.
 *
 * A job that has been queued prints nothing but nine spaces -- exactly
 * the width of `[  OK  ] ` -- then its name in dim grey, so the tag
 * column stays empty until the job answers.  A job that has finished
 * fills that column in with `[  OK  ]` and reports what it did.  The
 * machine boots in one pass with no scheduler to interleave the pairs,
 * but the shape is the point: it reads as a list of jobs in order rather
 * than a list of facts, and a unit that never prints its second line is
 * visible by the hole it leaves.
 */
void kernel_starting(const char *unit)
{
    console_set_color(NB_COL_GREY);
    console_write("         Starting ");
    console_write(unit);
    console_write("...\n");
    console_set_color(NB_COL_GREEN);
}

void kernel_started(const char *unit)
{
    char msg[56];
    char *d = put_str(msg, "Started ");

    d = put_str(d, unit);
    put_str(d, ".");
    kernel_ok(msg);
}

void kernel_target(const char *target)
{
    char msg[56];
    char *d = put_str(msg, "Reached target ");

    d = put_str(d, target);
    put_str(d, ".");
    kernel_ok(msg);
}

/*
 * Status line for the startup chime.  The rate comes out of the sample's
 * own header -- dividing on the CPU is not available -- so the line has
 * to be composed rather than quoted.
 */
void kernel_ok_sound(unsigned rate, unsigned vol)
{
    char msg[56];
    char *d = put_str(msg, "Startup chime playing (");

    d = put_num(d, rate);
    d = put_str(d, " Hz, volume ");
    d = put_num(d, vol);
    put_str(d, ")");
    kernel_ok(msg);
}

void kernel_banner(void)
{
    console_set_color(NB_COL_WHITE);
    console_write("NeoBench 0.1.9 m68k-aga\n");
}

/*
 * Hardware detection pass: what the machine actually has, probed, not
 * asserted.  FPU and MMU are answered by executing an instruction that
 * only exists when the feature does and catching the trap when it does
 * not; the CPU model falls out of the same technique (probe.c), memory is
 * answered by testing the last long of each megabyte of expansion space.
 *
 * NeoBench is a 68060-only OS and needs real fast RAM under it, so
 * anything else is a failed check, not a warning: the red tag says the
 * machine cannot run the rest of the system as intended even though the
 * log keeps going.
 */
void kernel_detect(void)
{
    char msg[56];
    char *d;
    unsigned fast;
    int cpu = nb_probe_cpu();
    int bfg;

    if (cpu == 60)
    {
        kernel_ok("CPU detected (Motorola 68060)");
        nb_fpu_enable_060();        /* a 68060 boots with its FPU off  */
    }
    else
    {
        d = put_str(msg, "CPU is not 68060 (Motorola ");
        d = put_str(d, cpu == 40 ? "68040" : cpu == 30 ? "68030" : "68020");
        d = put_str(d, ")");
        *d = '\0';
        kernel_fail(msg);
    }

    if (nb_probe_rtg())
        kernel_ok("RTG detected (hires 640x512 lace, 8 bpp)");
    else
        kernel_warn("RTG not present");

    if (nb_probe_mmu())
        kernel_ok("MMU detected");
    else
        kernel_warn("MMU not present");

    if (nb_probe_fpu())
        kernel_ok("FPU detected");
    else
        kernel_warn("FPU not present");

    /*
     * NeoBench runs on fast RAM, so the requirement is stated against
     * that figure and not the machine total: 128 MB is the floor it will
     * not run below, 136 MB is what it is built for.  The line reads the
     * same either way -- the tag colour is what says whether this
     * machine can take the rest of the boot, exactly as for the CPU.
     */
    fast = nb_probe_fast_mb();
    bfg = nb_bfg9060();
    /*
     * Probe internals are deliberately not part of the boot log: they go
     * to the serial port only, where they are diagnostic rather than
     * something a user reading the screen needs.
     */
    {
        extern unsigned nb_probe_dbg_top, nb_probe_dbg_bus;
        extern unsigned nb_probe_dbg_first, nb_probe_dbg_count;
        const char *p;

        d = put_str(msg, "probe f=");
        d = put_num(d, nb_probe_dbg_first);
        d = put_str(d, " c=");
        d = put_num(d, nb_probe_dbg_count);
        d = put_str(d, " l=");
        d = put_num(d, nb_probe_dbg_top);
        d = put_str(d, " bus=");
        d = put_num(d, nb_probe_dbg_bus);
        d = put_str(d, " bfg=");
        if (bfg >= 0)
            d = put_num(d, (unsigned)bfg);
        else
            d = put_str(d, "none");
        *d = '\0';

        amiga_serial_putc('>');
        for (p = msg; *p; p++)
            amiga_serial_putc(*p);
        amiga_serial_putc('\r');
        amiga_serial_putc('\n');
    }
    d = put_str(msg, "Memory detected (");
    d = put_num(d, fast);
    d = put_str(d, " MB fast, ");
    d = put_num(d, fast < NB_FAST_FLOOR ? NB_FAST_FLOOR : NB_FAST_PREFERRED);
    d = put_str(d, fast < NB_FAST_FLOOR ? " MB required)" : " MB preferred)");
    *d = '\0';

    if (fast < NB_FAST_FLOOR)
        kernel_fail(msg);
    else if (fast < NB_FAST_PREFERRED)
        kernel_warn(msg);
    else
        kernel_ok(msg);

    /*
     * The BFG9060, asked for the only way it can be found (probe.c), and
     * quiet when it is not: an A1200 cannot have one, and a signature
     * that does not answer does not say this machine could have.  The
     * miss is in the serial line above, where the rest of what the
     * probe saw goes -- a colour on the screen for the absence of a
     * card this machine has no slot for would be a rumour.
     */
    if (bfg >= 0)
    {
        d = put_str(msg, "BFG9060 detected (firmware ");
        d = put_num(d, (unsigned)bfg);
        d = put_str(d, ")");
        *d = '\0';
        kernel_ok(msg);
    }

    kernel_ok("UART detected (9600 baud)");
}

/*
 * Driver pass: walk every bus this machine has, and start a driver for
 * every device that answers.
 *
 * The walk and the report are deliberately not the same thing, because
 * they are not the same thing.  ide, cdrom and card are walked: Gayle
 * sits at a fixed address on this chipset, IDENTIFY answers for what is
 * behind it, and the first word of that answer says whether the unit is
 * a fixed disk or something somebody plugged into the socket -- which is
 * what makes a compact flash or SD adapter a card reader rather than a
 * hard disk, without needing a driver that knows the difference.
 *
 * scsi, usb, sata and net are not walked, and the lines below say what
 * the machine is rather than what a scan saw.  Their controllers live on
 * the expansion or the PCI bus, and space above $D80000 must not be read
 * blind from this ROM: a word that comes back is indistinguishable from
 * a device answering, which is precisely how the memory probe came to
 * report 1816 MB of fast RAM.  A scan that cannot be told from a rumour
 * is not a scan, so those four get the honest platform answer instead --
 * an AGA A1200 or A4000T carries no such controller at all.
 *
 * Every line is therefore one of two things: a device that answered and
 * a driver that is now bound to it, or a bus that carries nothing this
 * machine can have.  The two counts at the end count drivers actually
 * started, and nothing else.
 */
/*
 * Read one sector back through the device table, so that a storage line
 * in the log is backed by a transfer that really happened.  IDENTIFY
 * says a drive is there; this says its sectors come off it -- the two
 * are different claims, and the second one is what the rest of the
 * system will be standing on.
 *
 * The buffer is poisoned first, so a read that quietly left it alone
 * cannot pass for a read.  The serial line carries the measurement --
 * device, address and the first sixteen bytes exactly as they came
 * back -- while the log carries the verdict, and says what the device
 * reported when the verdict is no.
 */
static void serial_line(const char *s)
{
    while (*s)
        amiga_serial_putc(*s++);
    amiga_serial_putc('\r');
    amiga_serial_putc('\n');
}

/*
 * Which controller a failed read should be blamed on.  The format of
 * the line does not change with it -- only whose status register the
 * hex in it comes from, since a SCSI target and an IDE disk keep their
 * failures in two different places and one of them would be a lie
 * about the other.
 */
static unsigned dev_status(const struct nb_dev *d)
{
    return d->read == nb_scsi_read ? nb_scsi_status() : nb_ata_status();
}

static void report_read(const struct nb_dev *d, unsigned lba, uint8_t *buf)
{
    static const char hex[] = "0123456789abcdef";
    char line[80];
    char *p;
    unsigned i;
    int ok;

    for (i = 0; i < d->secsize; i++)
        buf[i] = 0xde;

    ok = d->read && nb_dev_read(d->name, 0, lba, buf, 1);

    p = put_str(line, ">blk ");
    p = put_str(p, d->name);
    p = put_str(p, " u=0 lba=");
    p = put_num(p, lba);
    p = put_str(p, ok ? " ok " : " fail st=$");
    if (ok)
    {
        for (i = 0; i < 16; i++)
        {
            *p++ = hex[buf[i] >> 4];
            *p++ = hex[buf[i] & 15u];
        }
    }
    else
        p = put_hex(p, dev_status(d), 2);
    *p = '\0';
    serial_line(line);

    p = put_str(line, "block read: ");
    p = put_str(p, d->name);
    p = put_str(p, " sector ");
    p = put_num(p, lba);
    if (ok)
    {
        p = put_str(p, " (");
        p = put_num(p, d->secsize);
        p = put_str(p, " bytes)");
        *p = '\0';
        kernel_ok(line);
    }
    else
    {
        p = put_str(p, " would not read (status $");
        p = put_hex(p, dev_status(d), 2);
        p = put_str(p, ")");
        *p = '\0';
        kernel_warn(line);
    }
}

static void dev_selftest(void)
{
    uint8_t buf[2048];
    const struct nb_dev *d;

    d = nb_dev_find("ata.device");
    if (d && d->units && d->read && d->secsize <= sizeof buf)
        report_read(d, 0, buf);            /* sector 0: where a disk boots */

    d = nb_dev_find("atapi.device");
    if (d && d->units && d->read && d->secsize <= sizeof buf)
        report_read(d, 16, buf);           /* 16: the CD's volume header   */

    d = nb_dev_find("scsi.device");
    if (d && d->units && d->read && d->secsize <= sizeof buf)
        report_read(d, 0, buf);            /* sector 0: where a disk boots */
}

/*
 * The install disc, and the install itself: two lines that say what the
 * machine found on the CD and what it did about it.
 *
 * The disc line is asked for whenever a CD answered -- an ISO 9660
 * volume that is not NeoBench's is reported just as plainly as one that
 * is, because the boot log's rule is that a question gets its answer.
 * The install line only runs on NeoBench's own disc, and every outcome
 * it can have is written down: written now, already there, refused over
 * someone else's data, damaged, no payload, no disk.  A run that does
 * nothing says so instead of staying quiet, since a silent installer
 * and a broken one look identical in a log otherwise.
 */
static void report_disc(void)
{
    const struct nb_dev *cd = nb_dev_find("atapi.device");
    struct nb_install ins;
    char msg[88];
    char *p;
    int iso;

    if (!cd || !cd->units || !cd->read)
        return;                     /* atapi.device has said why       */

    iso = nb_iso_mount();
    if (iso == NB_ISO_NONE)
    {
        kernel_warn("iso9660: disc carries no ISO 9660 volume");
        return;
    }

    p = put_str(msg, "iso9660: ");
    p = put_str(p, iso == NB_ISO_INSTALL ? "NeoBench installer disc \""
                                         : "volume \"");
    p = put_str(p, nb_iso_volume());
    p = put_str(p, "\", ");
    p = put_num(p, nb_iso_files());
    p = put_str(p, " files");
    *p = '\0';
    kernel_ok(msg);

    if (iso != NB_ISO_INSTALL)
        return;

    nb_install_run(&ins);
    switch (ins.state)
    {
    case NB_INSTALL_NO_PAYLOAD:
        kernel_warn("installer: disc carries no NBFS.IMG payload");
        return;
    case NB_INSTALL_NO_DISK:
        kernel_warn("installer: no disk to install to");
        return;
    case NB_INSTALL_REFUSED:
        kernel_warn("installer: hda is not blank; refusing to overwrite");
        return;
    case NB_INSTALL_DAMAGED:
        kernel_warn("installer: hda has a damaged NeoBench volume");
        return;
    case NB_INSTALL_PRESENT:
        p = put_str(msg, "installer: NeoBench already on hda (NBFS \"");
        p = put_str(p, ins.volume);
        p = put_str(p, "\")");
        break;
    case NB_INSTALL_DONE:
        p = put_str(msg, "installer: NeoBench written to hda (");
        p = put_num(p, ins.sectors >> 1);   /* 512-byte sectors -> KB */
        p = put_str(p, " KB)");
        break;
    default:
        p = put_str(msg, "installer: stream stopped (");
        p = put_str(p, ins.op ? ins.op : "?");
        p = put_str(p, ", lba ");
        p = put_num(p, ins.lba);
        p = put_str(p, ") after ");
        p = put_num(p, ins.sectors);
        p = put_str(p, " sectors");
        break;
    }
    *p = '\0';
    if (ins.state == NB_INSTALL_DONE)
        kernel_ok(msg);
    else
        kernel_warn(msg);
}

/*
 * Serial only, and before anything has been judged: what each unit left
 * on the task file when it was selected.  The boot log has one line per
 * device and no room for the unit that answered but would not identify,
 * which is exactly the case worth seeing -- a drive that is there and a
 * bus that is not can leave the same status behind, and it takes the
 * signature to tell them apart.
 */
static void report_ident(unsigned unit, const struct nb_ata_id *id)
{
    char line[72];
    char *p;

    p = put_str(line, ">ide u=");
    p = put_num(p, unit);
    p = put_str(p, " st=$");
    p = put_hex(p, id->status, 2);
    p = put_str(p, " sig=$");
    p = put_hex(p, id->sig, 4);
    p = put_str(p, " id=$");
    p = put_hex(p, id->ident, 4);
    *p = '\0';
    serial_line(line);
}

void kernel_drivers(void)
{
    struct nb_ata_id dev[2];
    const char *cd_model = 0;
    const char *card_model = 0;
    unsigned card_i = 0;
    unsigned disk_i = 0;
    unsigned cd_i = 0;
    unsigned ndisk = 0;
    unsigned ncd = 0;
    unsigned ncard = 0;
    unsigned bound = 0;
    unsigned pbound = 0;
    unsigned i;
    const struct nb_scsi_info *scsi;
    char msg[80];
    char *d;

    scsi = nb_scsi_probe();

    nb_ata_identify(0, &dev[0]);
    nb_ata_identify(1, &dev[1]);
    report_ident(0, &dev[0]);
    report_ident(1, &dev[1]);

    for (i = 0; i < 2; i++)
    {
        if (!dev[i].present)
            continue;
        if (dev[i].atapi)
        {
            if (!ncd)                   /* the first CD is this device's 0 */
                cd_i = i;
            ncd++;
            if (!cd_model)
                cd_model = dev[i].model;
        }
        else if (dev[i].ident & 0x80u)          /* IDENTIFY word 0, bit 7 */
        {
            if (!ncard)
                card_i = i;
            ncard++;
            if (!card_model)
                card_model = dev[i].model;
        }
        else
        {
            if (!ndisk)
                disk_i = i;
            ndisk++;
        }
    }

    /* ---- ide: the controller, then each fixed disk behind it ------- */
    if (ndisk || ncard)
    {
        kernel_ok("ata.device: Gayle IDE controller (PIO mode 0)");
        bound++;
    }
    else
    {
        d = put_str(msg, "ata.device: no device on the IDE bus (");
        d = put_st_id(d, dev[0].status, dev[0].ident);
        d = put_str(d, ")");
        *d = '\0';
        kernel_warn(msg);
    }

    for (i = 0; i < 2; i++)
    {
        if (!dev[i].present || dev[i].atapi || (dev[i].ident & 0x80u))
            continue;
        d = put_str(msg, i ? "  hdb: " : "  hda: ");
        d = put_str(d, dev[i].model[0] ? dev[i].model : "unnamed device");
        if (dev[i].mb)
        {
            d = put_str(d, ", ");
            d = put_num(d, dev[i].mb);
            d = put_str(d, " MB");
        }
        *d = '\0';
        kernel_ok(msg);
    }

    /* ---- cdrom ---------------------------------------------------- */
    if (ncd)
    {
        d = put_str(msg, "atapi.device: ");
        d = put_str(d, cd_model && *cd_model ? cd_model : "CD-ROM");
        *d = '\0';
        kernel_ok(msg);
        bound++;
    }
    else
    {
        /*
         * The unit that announced itself as a packet device, or else the
         * second one, which is where a CD sits when the bus also carries
         * a disk.  Either way, what that unit said when it was asked is
         * the whole difference between a bus with nothing on it and a
         * drive that is there but not answering yet.
         */
        unsigned cu = dev[0].sig == 0x14ebu ? 0 : 1;

        d = put_str(msg, "atapi.device: no CD-ROM on the IDE bus (");
        d = put_sig(d, dev[cu].sig);
        d = put_str(d, ", ");
        d = put_st_id(d, dev[cu].status, dev[cu].ident);
        d = put_str(d, ")");
        *d = '\0';
        kernel_warn(msg);
    }

    /* ---- card reader: a removable unit on the same bus ------------- */
    if (ncard)
    {
        d = put_str(msg, "sdcard.device: ");
        d = put_str(d, card_model && *card_model ? card_model
                                                 : "removable card");
        if (dev[card_i].mb)
        {
            d = put_str(d, ", ");
            d = put_num(d, dev[card_i].mb);
            d = put_str(d, " MB");
        }
        *d = '\0';
        kernel_ok(msg);
        pbound++;
    }
    else
        kernel_warn("sdcard.device: no removable card in the IDE socket");

    /*
     * ---- scsi: the one host an AGA machine may carry natively -------
     *
     * The A4000T motherboard carries an NCR53C710 and nothing else in
     * the family does, so this line is about the chip rather than about
     * the machine: three answers, all of them the driver's own
     * observation.  Targets that ran a command to completion get the
     * model and the size of the first one.  The chip with nothing on it
     * is a third thing, not a second case of the first, because it says
     * the adapter is there and the bus is what is empty.  No chip at all
     * is what an A1200 answers, and it answers it without a line being
     * written for it by name.
     */
    if (scsi->units)
    {
        d = put_str(msg, "scsi.device: ");
        if (scsi->model[0])
        {
            d = put_str(d, scsi->model);
            if (scsi->mb)
            {
                d = put_str(d, ", ");
                d = put_num(d, scsi->mb);
                d = put_str(d, " MB");
            }
        }
        else if (scsi->mb)
        {
            d = put_num(d, scsi->mb);
            d = put_str(d, " MB disk");
        }
        else
            d = put_str(d, "SCSI disk");
        d = put_str(d, " (target ");
        d = put_num(d, scsi->base);
        d = put_str(d, ")");
        *d = '\0';
        kernel_ok(msg);
        bound++;
    }
    else if (scsi->found)
    {
        d = put_str(msg, "scsi.device: NCR53C710 in the Gayle window at $DD00");
        d = put_hex(d, scsi->win, 2);
        d = put_str(d, ", no target answered");
        *d = '\0';
        kernel_warn(msg);
    }
    else
        kernel_warn("scsi.device: no NCR53C710 host in the Gayle window");

    /*
     * ---- zz9000: MNT Research's Zorro card and its AX module --------
     *
     * The one bus on this machine that can still turn up with something
     * on it, so it is asked rather than assumed.  zz9000.c reads the
     * autoconfig window, matches the card by manufacturer and product,
     * gives it an address from the map the probe already walked, and
     * then polls the card's own registers; everything this reports
     * therefore comes from the card or from the window it sits in, and
     * nothing from a table of what the machine is believed to hold.
     *
     * A card that is absent is a WARN and not a FAILED: no AGA machine
     * ships with one, and the driver has no reason to hold up the rest
     * of the boot.  A card that parses but cannot be given a window, or
     * that does not answer once it has one, is reported as that rather
     * than as absent, because the three are different faults and only
     * the first means "nothing there".
     */
    {
        const struct nb_zz9000 *zz = nb_zz9000_probe();

        if (zz->bound)
        {
            d = put_str(msg, "zz9000.device: MNT ZZ9000, Zorro ");
            *d++ = (char)('0' + zz->zorro);
            d = put_str(d, ", ");
            d = put_num(d, zz->size / 0x100000ul);
            d = put_str(d, " MB at $");
            d = put_hex(d, zz->base, 8);
            *d = '\0';
            kernel_ok(msg);
            pbound++;
        }
        else if (zz->present && zz->base)
        {
            d = put_str(msg, "zz9000.device: MNT ZZ9000 present, no register");
            d = put_str(d, " response at $");
            d = put_hex(d, zz->base, 8);
            *d = '\0';
            kernel_warn(msg);
        }
        else if (zz->present)
        {
            d = put_str(msg, "zz9000.device: MNT ZZ9000 present, no free");
            d = put_str(d, " window of ");
            d = put_num(d, zz->size / 0x100000ul);
            d = put_str(d, " MB");
            *d = '\0';
            kernel_warn(msg);
        }
        else
        {
            kernel_warn("zz9000.device: no MNT ZZ9000 in the autoconfig"
                        " window");
        }

        /*
         * The AX is not a second board but a module the card reports in
         * one bit of one register, so it can only be asked once the card
         * has an address -- which is exactly the order this runs in.
         */
        if (zz->bound)
        {
            d = put_str(msg, "zz9000ax.audio: ");
            if (zz->ax)
                d = put_str(d, "AX module fitted (firmware $");
            else
                d = put_str(d, "no AX module on the card (firmware $");
            d = put_hex(d, zz->fw, 4);
            d = put_str(d, ")");
            *d = '\0';
            if (zz->ax)
            {
                kernel_ok(msg);
                pbound++;
            }
            else
                kernel_warn(msg);
        }
        else
            kernel_warn("zz9000ax.audio: needs a configured ZZ9000");
    }

    /*
     * ---- the device table: what NeoBench has bound for itself --------
     *
     * The lines above say a driver started; this is where the system goes
     * to use one.  Each device that answered gets an entry carrying the
     * driver behind it and the units it may be asked for, so a caller
     * reads a sector by name rather than by knowing which controller the
     * name happens to sit on.
     *
     * A device that did not answer gets no entry at all.  A name that
     * could be looked up but would never work is worse than no name, and
     * it is the difference between "not on this machine" and "broken",
     * which the boot log above already draws.
     */
    {
        struct nb_dev d;

        d.name = 0;
        d.units = 0;
        d.base = 0;
        d.secsize = 0;
        d.read = 0;
        d.write = 0;

        if (ndisk || ncard)                 /* the controller itself     */
        {
            d.name = "ata.device";
            d.units = ndisk;
            d.base = disk_i;
            d.secsize = 512;
            d.read = nb_ata_read;
            d.write = nb_ata_write;
            nb_dev_add(&d);
        }
        if (ncd)
        {
            d.name = "atapi.device";
            d.units = ncd;
            d.base = cd_i;
            d.secsize = 2048;
            d.read = nb_atapi_read;
            d.write = 0;                     /* a CD-ROM is read only     */
            nb_dev_add(&d);
        }
        if (ncard)
        {
            d.name = "sdcard.device";
            d.units = ncard;
            d.base = card_i;
            d.secsize = 512;
            d.read = nb_ata_read;
            d.write = nb_ata_write;
            nb_dev_add(&d);
        }
        if (scsi->units)
        {
            /*
             * The host is one adapter and the targets are its units, so
             * the entry starts the numbering at the first target that
             * answered and counts the run of them.  Unit 0 is therefore
             * whichever disk came first, not whichever happened to be
             * wired to target 0 -- a bus that answers on 1 alone is one
             * disk, and calling it unit 1 would be a gap with no disk
             * behind it.
             */
            d.name = "scsi.device";
            d.units = scsi->units;
            d.base = scsi->base;
            d.secsize = 512;
            d.read = nb_scsi_read;
            d.write = nb_scsi_write;
            nb_dev_add(&d);
        }

        /*
         * Paula is on every machine this boots on, so its driver is
         * bound whether or not anything has been loaded to play through
         * it.  It has no sectors, which is what a null read and write
         * with a zero secsize say.
         */
        d.name = "sound.device";
        d.units = 1;
        d.base = 0;
        d.secsize = 0;
        d.read = 0;
        d.write = 0;
        nb_dev_add(&d);

        /*
         * Input, and the wire it can arrive on.  Neither is a bus with a
         * card to find: the keyboard is on CIA-A and the receiver is on
         * Paula, both there from the moment the machine came up, so both
         * drivers are bound whether or not anything has asked them for a
         * key.  They take no place in the storage or the peripheral
         * counts either -- not because they are less bound, but because
         * those two counts are about cards that may or may not have
         * answered, and there is no card to answer here (kbd.c).
         */
        d.name = "input.device";
        d.units = 1;
        d.base = 0;
        d.secsize = 0;
        d.read = 0;
        d.write = 0;
        nb_dev_add(&d);

        d.name = "serial.device";
        nb_dev_add(&d);

        /*
         * The Zorro card is in the table only when it took an address
         * and answered: nb_zz9000_probe() is one call per boot, so
         * asking it again here costs nothing and cannot disagree with
         * the lines above.
         */
        {
            const struct nb_zz9000 *zz = nb_zz9000_probe();

            d.name = 0;
            d.units = 0;
            d.base = 0;
            d.secsize = 0;
            d.read = 0;
            d.write = 0;

            if (zz->bound)
            {
                d.name = "zz9000.device";
                d.units = 1;
                nb_dev_add(&d);
            }
            if (zz->bound && zz->ax)
            {
                d.name = "zz9000ax.audio";
                d.units = 1;
                nb_dev_add(&d);
            }
        }
    }

    nb_dev_dump();
    dev_selftest();
    report_disc();

    /*
     * Input says where it is listening.  Unlike the lines below this one
     * is not a bus being asked a question -- it is two receivers on
     * hardware every machine this boots on has, polled rather than
     * interrupt driven because the one interrupt NeoBench owns is the
     * vertical blank (kbd.c).
     */
    kernel_ok("input.device: Amiga keyboard on CIA-A, port on Paula");

    /* ---- buses this machine cannot carry --------------------------- */
    d = put_str(msg, "usb.device: ");
    d = put_str(d, nb_usb_probe());
    *d = '\0';
    kernel_warn(msg);
    kernel_warn("sata.device: no PCI bus, unavailable on AGA");
    kernel_warn("net.device: no network controller on AGA");

    d = put_num(msg, bound);
    d = put_str(d, " of 5 storage drivers bound");
    *d = '\0';
    if (bound)
        kernel_ok(msg);
    else
        kernel_warn(msg);

    d = put_num(msg, pbound);
    d = put_str(d, " of 3 peripheral drivers bound");
    *d = '\0';
    if (pbound)
        kernel_ok(msg);
    else
        kernel_warn(msg);
}
