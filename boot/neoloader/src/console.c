#include "console.h"

/*
 * NeoBench serial console for NeoLoader.
 *
 * Default (Paula serial) drives the real Amiga serial port (SERDAT $DFF030
 * write / SERDATR $DFF018 status) for FS-UAE serial_port and real Amigas.
 *
 * Compile with -DNEOBENCH_UART_SERIAL to use the NeoBench board's CIA UART.
 */

#ifndef NEOBENCH_UART_SERIAL

#define SERDAT      (*(volatile unsigned short *)0xDFF030)
#define SERDATR     (*(volatile unsigned short *)0xDFF018)
#define SERPER      (*(volatile unsigned short *)0xDFF032)

#define SER_TBE     0x2000
#define SER_FRAME   0x3C00  /* 8-bit framing: bits 15-10 = stop/start */

static int serial_is_warm = 0;

void neo_putc(char c)
{
    if (c == '\n')
    {
        /* First char handling: if we have never sent, the TX buffer is
           empty so we may write immediately (no TBE wait). */
        if (!serial_is_warm)
        {
            SERPER = 368;   /* ~9600 baud, PAL */
            SERDAT = (unsigned short)(SER_FRAME | (unsigned char)'\r');
            serial_is_warm = 1;
        }
        else
        {
            while (!(SERDATR & SER_TBE))
                ;
            SERDAT = (unsigned short)(SER_FRAME | (unsigned char)'\r');
        }
        return;
    }

    if (!serial_is_warm)
    {
        SERPER = 368;   /* ~9600 baud, PAL */
        SERDAT = (unsigned short)(SER_FRAME | (unsigned char)c);
        serial_is_warm = 1;
        return;
    }
    while (!(SERDATR & SER_TBE))
        ;
    SERDAT = (unsigned short)(SER_FRAME | (unsigned char)c);
}

#else

#define CIAA_PRA        (*(volatile unsigned char *)0xBFE001)
#define SERIAL_DATA     (*(volatile unsigned char *)0xBFD100)

static void serial_wait_tbe(void)
{
    while (!(CIAA_PRA & 0x40))
        ;
}

void neo_putc(char c)
{
    if (c == '\n')
    {
        serial_wait_tbe();
        SERIAL_DATA = '\r';
    }
    serial_wait_tbe();
    SERIAL_DATA = c;
}

#endif

void neo_puts(const char *s)
{
    while (*s)
        neo_putc(*s++);
}

void neo_puthex(unsigned long value)
{
    static const char hex[] = "0123456789ABCDEF";
    neo_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        neo_putc(hex[(value >> i) & 0xF]);
}
