#include <stdint.h>
#include "mmu.h"

/*
 * 68060 MMU programming is deliberately isolated here.  The first porting
 * milestone only establishes the API; actual root/page-table programming
 * will be enabled after the FreeBSD m68k VM layout is wired in.
 */

void neobench_mmu_early_init(void)
{
    /* Keep translation disabled until the MD VM layer owns the tables. */
}

void neobench_mmu_enable(void)
{
    /* Implemented with the final FreeBSD m68k VM bootstrap. */
}

void neobench_mmu_disable(void)
{
    /* Implemented with the final FreeBSD m68k VM teardown path. */
}
