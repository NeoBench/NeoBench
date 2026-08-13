#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/vfs/vfs.h"

extern void kernel_banner(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    (void)boot;

    console_init();

    kernel_banner();

    if (vfs_init() != 0)
    {
        console_write("VFS:       FAILED\n");

        while (1)
            __asm__ volatile ("nop");
    }

    console_write("\n");
    console_write("NeoBench kernel ready.\n");

    while (1)
    {
        __asm__ volatile ("nop");
    }
}
