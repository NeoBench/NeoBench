#include "elf_jump.h"

void elf_jump(uint32_t entry, const nb_bootinfo_t *boot)
{
    nb_kernel_entry_t kernel =
        (nb_kernel_entry_t)(uintptr_t)entry;

    kernel(boot);

    /*
     * The kernel should never return.
     * If it does, halt forever.
     */
    for (;;)
    {
        __asm__ volatile ("nop");
    }
}
