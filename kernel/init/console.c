#include "../include/console.h"
#include "../../boot/rom/amiga.h"

void console_init(void)
{
    amiga_display_init();
}

void console_putc(char c)
{
    amiga_putc(c);
}

void console_write(const char *s)
{
    while (*s)
        console_putc(*s++);
}
