#include <kernel/kernel.h>

void panic(const char *msg)
{
    (void)msg;

    for (;;)
    {
        __asm__ volatile ("stop #0x2700");
    }
}
