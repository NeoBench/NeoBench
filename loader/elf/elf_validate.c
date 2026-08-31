#include <stdint.h>

#include "elf.h"
#include "elf_loader.h"

int elf_validate(const void *image)
{
    const elf32_header_t *hdr =
        (const elf32_header_t *)image;

    if (hdr == 0)
        return 0;

    /* ELF magic */
    if (hdr->e_ident[0] != 0x7F ||
        hdr->e_ident[1] != 'E'  ||
        hdr->e_ident[2] != 'L'  ||
        hdr->e_ident[3] != 'F')
    {
        return 0;
    }

    /* ELF32 */
    if (hdr->e_ident[4] != 1)
        return 0;

    /* Big endian */
    if (hdr->e_ident[5] != 2)
        return 0;

    /* Current ELF version */
    if (hdr->e_ident[6] != 1)
        return 0;

    /* Executable */
    if (hdr->e_type != 2)
        return 0;

    /* Motorola 68000 / m68k */
    if (hdr->e_machine != 4)
        return 0;

    /* Program-header structure */
    if (hdr->e_phentsize !=
        sizeof(elf32_program_header_t))
    {
        return 0;
    }

    if (hdr->e_phnum == 0)
        return 0;

    /* Entry must be non-zero */
    if (hdr->e_entry == 0)
        return 0;

    return 1;
}
