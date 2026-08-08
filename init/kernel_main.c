#include "../include/kernel.h"
#include "../include/console.h"

extern void kernel_banner(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    console_init();

    kernel_banner();

    console_write("Kernel started\n");

    if (boot)
    {
        console_write("Boot info OK\n");
    }

    while (1)
    {
        __asm__ volatile ("nop");
    }
}
