#include "../include/console.h"
#include "../../boot/rom/amiga.h"

/*
 * Linux-style boot log.
 *
 * No banner art, no rule lines: a single header line, then one fixed-width
 * status tag per subsystem -- `[  OK  ]` in the state colour with the
 * message in phosphor white, exactly how systemd's log reads.  The tag
 * column never moves, so a failure or warning is visible by colour alone
 * without re-reading the text.
 */

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

void kernel_banner(void)
{
    console_set_color(NB_COL_WHITE);
    console_write("NeoBench 0.1.0 m68k-aga\n");

    kernel_ok("Started aga chipset.");
    kernel_ok("Started display (hires 640x256, 8 bitplanes).");
    kernel_ok("Started serial console (9600 baud).");
}
