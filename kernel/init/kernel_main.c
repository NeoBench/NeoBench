#include "../include/kernel.h"
#include "../include/console.h"
#include "../include/block/device.h"
#include "../include/nbfs.h"
#include "../include/vfs/vfs.h"

extern void kernel_banner(void);

void kernel_main(const nb_bootinfo_t *boot)
{
    (void)boot;

    console_init();

    kernel_banner();

    /*
     * Block device layer.
     */
    console_write("BLOCK:     initializing...\n");

    /*
     * The physical device will be supplied by the boot/device
     * layer. For now the block subsystem itself is brought up.
     */
    console_write("BLOCK:     ready\n");

    /*
     * NBFS kernel layer.
     */
    console_write("NBFS:      initializing...\n");

    if (nbfs_kernel_init() != 0)
    {
        console_write("NBFS:      FAILED\n");

        while (1)
            __asm__ volatile ("nop");
    }

    console_write("NBFS:      ready\n");

    /*
     * VFS layer.
     */
    console_write("VFS:       initializing...\n");

    if (vfs_init() != 0)
    {
        console_write("VFS:       FAILED\n");

        while (1)
            __asm__ volatile ("nop");
    }

    console_write("VFS:       ready\n");

    console_write("\n");
    console_write("NeoBench kernel ready.\n");

    while (1)
    {
        __asm__ volatile ("nop");
    }
}
