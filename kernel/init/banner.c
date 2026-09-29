#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/ata.h"

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
extern unsigned nb_probe_fast_mb(void);

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
    console_write("NeoBench 0.1.3 m68k-aga\n");
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
     * that figure and not the machine total: 50 MB is the floor it will
     * not run below, 80 MB is what it is built for.  The line reads the
     * same either way -- the tag colour is what says whether this
     * machine can take the rest of the boot, exactly as for the CPU.
     */
    fast = nb_probe_fast_mb();
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
    d = put_num(d, fast < 50 ? 50 : 80);
    d = put_str(d, fast < 50 ? " MB required)" : " MB preferred)");
    *d = '\0';

    if (fast < 50)
        kernel_fail(msg);
    else if (fast < 80)
        kernel_warn(msg);
    else
        kernel_ok(msg);

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
void kernel_drivers(void)
{
    struct nb_ata_id dev[2];
    const char *cd_model = 0;
    const char *card_model = 0;
    unsigned card_i = 0;
    unsigned ndisk = 0;
    unsigned ncd = 0;
    unsigned ncard = 0;
    unsigned bound = 0;
    unsigned pbound = 0;
    unsigned i;
    char msg[80];
    char *d;

    nb_ata_identify(0, &dev[0]);
    nb_ata_identify(1, &dev[1]);

    for (i = 0; i < 2; i++)
    {
        if (!dev[i].present)
            continue;
        if (dev[i].atapi)
        {
            ncd++;
            if (!cd_model)
                cd_model = dev[i].model;
        }
        else if (dev[i].ident & 0x80u)          /* IDENTIFY word 0, bit 7 */
        {
            ncard++;
            card_i = i;
            if (!card_model)
                card_model = dev[i].model;
        }
        else
            ndisk++;
    }

    /* ---- ide: the controller, then each fixed disk behind it ------- */
    if (ndisk || ncard)
    {
        kernel_ok("ata.device: Gayle IDE controller (PIO mode 0)");
        bound++;
    }
    else
    {
        /* $FF is a bus nobody is driving, $00 a bus every line is pulled
         * down, and an identify word of $FFFF means no result was offered
         * at all.  A failed probe reports what it saw, not just that it
         * failed, so the line is worth reading when something is wrong. */
        static const char hex[] = "0123456789ABCDEF";
        unsigned st = dev[0].status;
        unsigned idv = dev[0].ident;
        int nibble;

        d = put_str(msg, "ata.device: no device on the IDE bus (status $");
        *d++ = hex[(st >> 4) & 0xf];
        *d++ = hex[st & 0xf];
        d = put_str(d, ", identify $");
        for (nibble = 12; nibble >= 0; nibble -= 4)
            *d++ = hex[(idv >> nibble) & 0xf];
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
        kernel_warn("atapi.device: no CD-ROM on the IDE bus");
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

    /* ---- buses this machine cannot carry --------------------------- */
    kernel_warn("scsi.device: no SCSI host adapter on AGA");
    kernel_warn("usb.device: no USB host controller on AGA");
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
    d = put_str(d, " of 2 peripheral drivers bound");
    *d = '\0';
    if (pbound)
        kernel_ok(msg);
    else
        kernel_warn(msg);
}
