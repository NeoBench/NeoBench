#ifndef NEOLOADER_ELF_LOADER_H
#define NEOLOADER_ELF_LOADER_H

#include <stdint.h>

#define NEO_ELF_OK          0
#define NEO_ELF_BAD_MAGIC  -1
#define NEO_ELF_BAD_CLASS  -2
#define NEO_ELF_BAD_ENDIAN -3
#define NEO_ELF_BAD_TYPE   -4
#define NEO_ELF_BAD_MACHINE -5
#define NEO_ELF_BAD_HEADER -6
#define NEO_ELF_BAD_PROGRAM -7
#define NEO_ELF_NO_LOAD    -8

typedef struct {
    uint32_t entry;
    uint32_t image_start;
    uint32_t image_end;
    uint32_t load_count;
} neo_elf_info_t;

int neo_elf_load(
    const void *image,
    uint32_t image_size,
    neo_elf_info_t *info
);

/*
 * Load an ELF image directly into its physical/virtual
 * destination addresses.
 *
 * The target m68k loader uses p_paddr when non-zero,
 * otherwise p_vaddr.
 */
int neo_elf_load_memory(
    const void *image,
    uint32_t image_size,
    neo_elf_info_t *info
);

#endif
