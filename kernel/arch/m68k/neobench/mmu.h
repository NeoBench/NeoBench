#ifndef NEOBENCH_M68K_MMU_H
#define NEOBENCH_M68K_MMU_H

#include <stdint.h>

/* 68060 CACR/TC/TT register interfaces used by the machine port. */
#define NEOBENCH_M68K_CACR_ENABLE_I 0x00000001u
#define NEOBENCH_M68K_CACR_ENABLE_D 0x00000100u

struct neobench_mmu_bootinfo {
    uintptr_t ram_base;
    uintptr_t ram_size;
    uintptr_t kernel_base;
    uintptr_t kernel_size;
};

void neobench_mmu_early_init(const struct neobench_mmu_bootinfo *boot);

#endif
