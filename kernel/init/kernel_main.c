#include "../include/kernel.h"
#include "../include/console.h"

extern void kernel_banner(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    /* Boot information will be used later */
    (void)boot;

    console_init();

    kernel_banner();

    console_write("Kernel started\n");

    while (1)
    {
        __asm__ volatile ("nop");
    }
}
