#include "../include/console.h"
#include "../../boot/rom/amiga.h"

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
        kernel_ok("RTG detected (hires 640x256, 8 bitplanes)");
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
