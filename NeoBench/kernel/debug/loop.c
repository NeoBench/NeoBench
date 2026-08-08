#include <kernel/kernel.h>

void kernel_loop(void)
{
    for (;;)
    {
        __asm__ volatile ("nop");
    }
}
