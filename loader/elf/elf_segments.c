#include "elf.h"

int elf_load(const void *image)
{
    const elf32_header_t *hdr = image;

    const elf32_program_header_t *ph =
        (const elf32_program_header_t *)
        ((const uint8_t *)image + hdr->e_phoff);

    for (uint16_t i = 0; i < hdr->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;

        /*
         * Copy segment to destination.
         * Zero any remaining bytes up to p_memsz.
         */
    }

    return 1;
}
