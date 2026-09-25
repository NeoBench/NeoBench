#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"

extern void kernel_banner(void);
extern void kernel_ok(const char *msg);

/* Boot self-test result, reported as `name  [bar] status`. */
enum
{
    NB_TEST_OK   = 0,                  /* verdict printed in green  */
    NB_TEST_FAIL = 1,                  /* red                       */
    NB_TEST_WARN = 2                   /* amber                     */
};

/*
 * One status line of the boot self-test:
 *
 *     rtg  [**********************] ok
 *
 * The label is phosphor white, the bar and the verdict share the state
 * colour, so a failed test reads entirely red and a degraded one amber
 * while the bar length stays constant and nothing shifts sideways.
 */
static void console_status(const char *name, int state)
{
    unsigned color;

    if (state == NB_TEST_FAIL)
        color = NB_COL_RED;
    else if (state == NB_TEST_WARN)
        color = NB_COL_AMBER;
    else
        color = NB_COL_GREEN;

    console_set_color(NB_COL_WHITE);
    console_write(name);
    console_write("  [");

    console_set_color(color);
    console_write("**********************] ");

    if (state == NB_TEST_FAIL)
        console_write("fail\n");
    else if (state == NB_TEST_WARN)
        console_write("warning\n");
    else
        console_write("ok\n");

    console_set_color(NB_COL_GREEN);
}

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    amiga_serial_putc('K');
    console_init();
    amiga_serial_putc('C');
    kernel_banner();
    amiga_serial_putc('B');

    kernel_ok("System detected.");
    console_status("rtg", NB_TEST_OK);

    for (;;)
        amiga_display_vsync();
}
