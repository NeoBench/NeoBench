#include <stdint.h>
#include "bootinfo.h"

/*
 * Platform bootstrap boundary.  Keep this code machine-specific so the
 * eventual FreeBSD MD layer can consume the same boot contract.
 */
static const struct nb_bootinfo *nb_bootinfo;

void neobench_platform_init(const struct nb_bootinfo *bi)
{
    if (!bi || bi->magic != NB_BOOTINFO_MAGIC || bi->version != NB_BOOTINFO_VERSION)
        return;

    nb_bootinfo = bi;
}

const struct nb_bootinfo *neobench_platform_bootinfo(void)
{
    return nb_bootinfo;
}

uint32_t neobench_platform_ram_base(void)
{
    if (!nb_bootinfo || nb_bootinfo->mem_count == 0)
        return 0;
    return nb_bootinfo->mem[0].base;
}

uint32_t neobench_platform_ram_size(void)
{
    if (!nb_bootinfo || nb_bootinfo->mem_count == 0)
        return 0;
    return nb_bootinfo->mem[0].size;
}
