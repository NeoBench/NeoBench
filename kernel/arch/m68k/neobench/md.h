#ifndef NEOBENCH_MD_H
#define NEOBENCH_MD_H

#include <stdint.h>
#include "bootinfo.h"
#include "trapframe.h"

void neobench_platform_init(const struct nb_bootinfo *bi);
const struct nb_bootinfo *neobench_platform_bootinfo(void);
uint32_t neobench_platform_ram_base(void);
uint32_t neobench_platform_ram_size(void);

void neobench_mmu_early_init(void);
void neobench_mmu_enable(void);
void neobench_mmu_disable(void);

void neobench_trap(struct neobench_trapframe *tf);

#endif
