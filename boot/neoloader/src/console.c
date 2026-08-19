#include "console.h"

void neo_puts(const char *s)
{
    /*
     * Temporary loader console.
     *
     * Replace this with the actual Amiga serial/RTG console
     * once NeoLoader's hardware layer is connected.
     */
    (void)s;
}

void neo_puthex(unsigned long value)
{
    (void)value;
}
