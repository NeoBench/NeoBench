#include <stdint.h>

#include "console.h"
#include "loader.h"
#include "bootart.h"

extern const unsigned char
_binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start[];

extern const unsigned char
_binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_end[];

void neo_main(void)
{
    neo_puts("\033[2J\033[H");

    /* Big 3D ASCII boot art. */
    bootart_draw_3d_wordmark();      /* extruded 3D "NEOBENCH"           */
    bootart_draw_cube();             /* animated rotating 3D cube        */
    bootart_draw_underline();        /* shaded rule                       */
    bootart_draw_footer("  NeoBench OS - NeoLoader v2 <68k> 3D boot");

    neo_puts("\n  Loading NeoBench kernel...\n\n");

    neo_puts("  Kernel ELF ............");

    const void *kernel_image =
        _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start;

    uint32_t kernel_size =
        (uint32_t)(
            _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_end -
            _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start
        );

    neo_puts(" \033[32m[ok]\033[0m\n");
    neo_puts("  Entry point ...........");

    int rc = neo_load_kernel(kernel_image, kernel_size);

    if (rc == 0)
    {
        neo_puts(" \033[32m[ok]\033[0m\n");
        neo_puts("  Jumping to kernel ......\n");
    }
    else
    {
        neo_puts(" \033[31m[fail]\033[0m\n");
        neo_puts("  NeoLoader halted.\n");
    }

    for (;;) {
        __asm__ volatile ("nop");
    }
}
