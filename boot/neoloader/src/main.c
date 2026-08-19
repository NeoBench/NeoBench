#include <stdint.h>

#include "loader.h"

extern const unsigned char
_binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start[];

extern const unsigned char
_binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_end[];

void neo_main(void)
{
    const void *kernel_image =
        _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start;

    uint32_t kernel_size =
        (uint32_t)(
            _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_end -
            _binary__home_lordp_NeoBench_rootfs_boot_kernel_elf_start
        );

    (void)neo_load_kernel(kernel_image, kernel_size);

    for (;;) {
        __asm__ volatile ("nop");
    }
}
