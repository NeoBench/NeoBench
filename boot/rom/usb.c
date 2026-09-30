/*
 * The USB host driver, bus half: walk the windows a card can describe
 * itself from, and say what is in them.
 *
 * The walk is the ZZ9000 driver's, over the same two windows and with
 * the same guard -- the Zorro III window is read only when the address
 * bus carries all 32 bits (probe.h), because on a 24-bit machine those
 * addresses are not decoded at all and reading them blind is the thing
 * this system does not do.  It is read only, it stops at the first slot
 * that does not parse as a board, and it writes nothing (zorro.h).
 *
 * The answer is built from the walk plus the fact that decides it: every
 * host controller NeoBench could name for an Amiga sits on PCI, and AGA
 * has no PCI bus.  A card that changed that would be a card this system
 * has no way to identify from a configuration ROM carrying no class
 * code, so the count goes in the line rather than a verdict about cards
 * it has only looked at.
 */
#include <stdint.h>
#include "usb.h"
#include "probe.h"
#include "zorro.h"

/* The two configuration windows, and how far each is walked: the same
 * numbers zz9000.c uses, for the same reason (zorro.h). */
#define NB_AUTO_Z2      0x00e80000UL
#define NB_AUTO_Z3      0xff000000UL
#define NB_SLOTS        16u
#define NB_USB_BOARDS   8u            /* more than a chain can hold here  */

static char reason[64];

static char *put_s(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *put_u(char *p, unsigned v)
{
    char b[11];
    int i = 11;

    b[--i] = '\0';
    do
    {
        b[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (b[i])
        *p++ = b[i++];
    return p;
}

const char *nb_usb_probe(void)
{
    struct nb_zdev boards[NB_USB_BOARDS];
    unsigned n;
    char *p = reason;

    n = nb_zorro_scan((const volatile uint8_t *)NB_AUTO_Z2, NB_SLOTS,
                      boards, NB_USB_BOARDS);
    if (n < NB_USB_BOARDS && nb_probe_bus())
        n += nb_zorro_scan((const volatile uint8_t *)NB_AUTO_Z3, NB_SLOTS,
                           boards + n, NB_USB_BOARDS - n);

    p = put_s(p, "no host controller (AGA has no PCI bus; Zorro: ");
    p = put_u(p, n);
    p = put_s(p, n == 1 ? " card)" : " cards)");
    *p = '\0';
    return reason;
}
