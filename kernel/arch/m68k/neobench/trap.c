#include <stdint.h>
#include "trapframe.h"

/*
 * MD-to-MI trap boundary.  Keep vector decoding here until the FreeBSD m68k
 * trap subsystem is imported; this prevents the NeoBench bootstrap from
 * inventing an incompatible machine-independent ABI.
 */
void neobench_trap(struct neobench_trapframe *tf)
{
    if (!tf)
        return;

    /* First milestone: preserve the frame and provide a debugger-visible hook. */
    (void)tf->vector;
    (void)tf->format;
}
