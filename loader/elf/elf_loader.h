#ifndef NB_ELF_LOADER_H
#define NB_ELF_LOADER_H

#include <stdint.h>

int elf_validate(const void *image);

int elf_load(const void *image);

void elf_jump(uint32_t entry);

#endif
