#ifndef NB_ELF_JUMP_H
#define NB_ELF_JUMP_H

#include <stdint.h>
#include "../../kernel/include/boot/bootinfo.h"

/*
 * NeoBench kernel entry point.
 *
 * The loader transfers control here after:
 *   - Loading all PT_LOAD segments
 *   - Clearing BSS
 *   - Initializing boot_info
 */
typedef void (*nb_kernel_entry_t)(const nb_bootinfo_t *boot);

void elf_jump(uint32_t entry, const nb_bootinfo_t *boot);

#endif
