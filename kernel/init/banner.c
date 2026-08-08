#include "../include/console.h"

void kernel_banner(void)
{
    console_write("\n");
    console_write("============================\n");
    console_write(" NeoBench Operating System\n");
    console_write(" Version 0.1.0\n");
    console_write(" Motorola 68060 Kernel\n");
    console_write("============================\n");
}
