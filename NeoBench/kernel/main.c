#include <kernel/kernel.h>

#include <arch/cpu.h>

void kernel_main(void)
{
    cpu_init();

    mmu_init();

    fpu_init();

    kernel_init();

    scheduler_init();

    kernel_loop();
}
