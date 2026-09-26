#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"

extern void kernel_banner(void);
extern void kernel_detect(void);
extern void kernel_drivers(void);
extern void kernel_ok(const char *msg);
extern void nb_desktop_render(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    amiga_serial_putc('K');
    console_init();
    amiga_serial_putc('C');
    kernel_banner();
    amiga_serial_putc('B');

    kernel_detect();
    amiga_serial_putc('D');

    kernel_drivers();
    amiga_serial_putc('R');

    kernel_ok("System detected.");

    /* Desktop scene replaces the boot log on screen. */
    kernel_ok("Reached target Graphical Interface.");
    amiga_serial_putc('G');
    nb_desktop_render();

    for (;;)
        amiga_display_vsync();
}
