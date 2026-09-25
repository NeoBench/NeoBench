#include "../include/kernel.h"
#include "../include/console.h"
#include "../../boot/rom/amiga.h"

extern void kernel_banner(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    amiga_serial_putc('K');
    console_init();
    amiga_serial_putc('C');
    kernel_banner();
    amiga_serial_putc('B');

    console_write("Kernel started\n");

    for (;;)
        amiga_display_vsync();
}
