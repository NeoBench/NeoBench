#include "../include/console.h"
#include "../include/console_color.h"

/*
 * NeoBench serial console driver.
 *
 * Default (Paula serial): drives the real Amiga serial port through the
 * Paula UART (SERDAT $DFF030 write / SERDATR $DFF018 read). This is what
 * FS-UAE's serial_port emulates and what real Amigas expose as RS-232, so
 * the console appears on both.
 *
 * Optional (neobench board UART): compile with -DNEOBENCH_UART_SERIAL to
 * use the NeoBench FPGA board's CIA-based UART ($BFD100 TX / $BFE001
 * status) instead.
 */

/* Paula serial. */
#define SERDAT      (*(volatile unsigned short *)0xDFF030)
#define SERDATR     (*(volatile unsigned short *)0xDFF018)
#define SERPER      (*(volatile unsigned short *)0xDFF032)
#define INTREQ      (*(volatile unsigned short *)0xDFF09C)

#define SER_TBE     0x2000   /* SERDATR bit 13: transmit buffer empty */
#define SER_FRAME   0x3C00   /* SERDAT 8-bit framing: bits 15-10      */

/* NeoBench board UART (optional). */
#define CIAA_PRA    (*(volatile unsigned char *)0xBFE001)
#define SERIAL_DATA (*(volatile unsigned char *)0xBFD100)

#ifndef NEOBENCH_UART_SERIAL

static int serial_is_warm = 0;

void console_init(void)
{
    /* ~9600 baud, 8-bit, PAL. Do NOT wait for TBE here: the transmit
       buffer is empty after reset, and some emulators only assert TBE
       once a real transmission has been clocked. Writing anyway gets
       the serial bus moving. */
    SERPER = 368;
    serial_is_warm = 0;
}

void console_putc(char c)
{
    if (!serial_is_warm)
    {
        SERDAT = (unsigned short)(SER_FRAME | (unsigned char)c);
        serial_is_warm = 1;
        return;
    }
    if (c == '\n')
    {
        while (!(SERDATR & SER_TBE))
            ;
        SERDAT = (unsigned short)(SER_FRAME | (unsigned char)'\r');
        return;
    }
    while (!(SERDATR & SER_TBE))
        ;
    SERDAT = (unsigned short)(SER_FRAME | (unsigned char)c);
}

#else

static void serial_wait_tbe(void)
{
    /* Wait for TX buffer empty (CIAA PRA bit 6). */
    while (!(CIAA_PRA & 0x40))
        ;
}

void console_init(void)
{
    serial_wait_tbe();
}

void console_putc(char c)
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

void console_write(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_write_color(const char *color, const char *s)
{
    console_write(color);
    console_write(s);
    console_write(NB_COLOR_RESET);
}

void console_write_bold(const char *s)
{
    console_write(NB_COLOR_BOLD);
    console_write(s);
    console_write(NB_COLOR_RESET);
}
