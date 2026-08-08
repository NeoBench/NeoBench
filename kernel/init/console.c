#include "../include/console.h"


volatile unsigned short *video =
    (unsigned short *)0xB8000;


static int pos = 0;


void console_init(void)
{
    pos = 0;
}


void console_putc(char c)
{
    video[pos++] =
        ((unsigned short)0x07 << 8) | c;
}


void console_write(const char *s)
{
    while (*s)
    {
        console_putc(*s++);
    }
}
