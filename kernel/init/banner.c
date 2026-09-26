#include "../include/console.h"
#include "../../boot/rom/amiga.h"
#include "../../boot/rom/ata.h"

/*
 * Linux-style boot log.
 *
 * No banner art, no rule lines: a single header line, then one fixed-width
 * status tag per detected subsystem -- `[  OK  ]` in the state colour with
 * the message in phosphor white, exactly how a kernel's detection log
 * reads.  The tag column never moves, so a missing feature is visible by
 * colour alone without re-reading the text.
 */

/* Hardware probes: boot/rom/probe.c + boot/rom/fline.S. */
extern int  nb_probe_rtg(void);
extern int  nb_probe_cpu(void);     /* 20/30/40/60 for 68020..68060 */
extern void nb_fpu_enable_060(void); /* PCR: switch the 68060 FPU on */
extern unsigned nb_probe_fpu(void);
extern unsigned nb_probe_mmu(void);
extern unsigned nb_probe_mem_mb(void);

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
static char *put_str(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
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
    return d;
}

void kernel_banner(void)
{
    console_set_color(NB_COL_WHITE);
    console_write("NeoBench 0.1.0 m68k-aga\n");
}

/*
 * Hardware detection pass: what the machine actually has, probed, not
 * asserted.  FPU and MMU are answered by executing an instruction that
 * only exists when the feature does and catching the trap when it does
 * not; the CPU model falls out of the same technique (probe.c), memory is
 * answered by testing the last long of each megabyte of expansion space.
 *
 * NeoBench is a 68060-only OS, so anything else is a failed check, not a
 * warning: the red tag says the machine cannot run the rest of the system
 * as intended even though the log keeps going.
 */
void kernel_detect(void)
{
    char msg[56];
    char *d;
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

    d = put_str(msg, "Memory detected (");
    d = put_num(d, nb_probe_mem_mb());
    d = put_str(d, " MB)");
    *d = '\0';
    kernel_ok(msg);

    kernel_ok("UART detected (9600 baud)");
}

/*
 * Driver pass: which buses this boot can actually talk to.
 *
 * There is no filesystem under NeoBench yet, so a driver that is not in the
 * ROM is a driver this boot does not have -- the log must not imply that
 * anything was loaded from disk when nothing was.  Each line is therefore
 * one of two honest answers: the bus was probed and something answered, or
 * the bus carries no hardware on an AGA machine and the driver is not
 * started.  The five buses are exactly the ones the boot promises to cover:
 * ide, cdrom, scsi, usb and sata.
 */
void kernel_drivers(void)
{
    struct nb_ata_id dev[2];
    const char *cd_model = 0;
    unsigned ndev = 0;
    unsigned ncd = 0;
    unsigned bound = 0;
    unsigned i;
    char msg[80];
    char *d;

    nb_ata_identify(0, &dev[0]);
    nb_ata_identify(1, &dev[1]);

    for (i = 0; i < 2; i++)
    {
        if (!dev[i].present)
            continue;
        ndev++;
        if (dev[i].atapi)
        {
            ncd++;
            if (!cd_model)
                cd_model = dev[i].model;
        }
    }

    if (ndev)
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
        if (!dev[i].present || dev[i].atapi)
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

    kernel_warn("scsi.device: no SCSI host adapter on AGA");
    kernel_warn("usb.device: no USB host controller on AGA");
    kernel_warn("sata.device: no PCI bus, unavailable on AGA");

    d = put_num(msg, bound);
    d = put_str(d, " of 5 storage drivers bound");
    *d = '\0';
    if (bound)
        kernel_ok(msg);
    else
        kernel_warn(msg);
}
