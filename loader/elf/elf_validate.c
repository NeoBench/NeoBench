#include "elf.h"

int elf_validate(const void *image)
{
    const elf32_header_t *hdr = image;

    if (hdr->e_ident[0] != 0x7F)
        return 0;

    if (hdr->e_ident[1] != 'E')
        return 0;

    if (hdr->e_ident[2] != 'L')
        return 0;

    if (hdr->e_ident[3] != 'F')
        return 0;

    return 1;
}
