#include <stdint.h>

/* Platform hooks implemented as the Amiga machine port grows. */
void neobench_early_init(void)
{
    /*
     * Phase 1 deliberately does not touch chipset/MMU registers yet.
     * The next stage will install the 68060 exception vectors, establish
     * cache/MMU policy, and consume the NeoLoader boot information block.
     */
}

void neobench_kernel_entry(void)
{
    /* Handoff point for the FreeBSD machine-independent kernel entry. */
}
