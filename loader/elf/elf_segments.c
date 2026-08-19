#include <stdint.h>
#include "elf.h"
#include "elf_loader.h"

#include "../../shared/libc/memory.h"

int elf_load(const void *image)
{
    const elf32_header_t *hdr =
        (const elf32_header_t *)image;

    const uint8_t *base =
        (const uint8_t *)image;

    const elf32_program_header_t *ph =
        (const elf32_program_header_t *)
        (base + hdr->e_phoff);

    for (uint16_t i = 0; i < hdr->e_phnum; i++)
    {
        if (ph[i].p_type != PT_LOAD)
            continue;

        /*
         * Basic sanity checks.
         */
        if (ph[i].p_filesz > ph[i].p_memsz)
            return 0;

        /*
         * Source is inside the ELF image.
         */
        const void *src =
            base + ph[i].p_offset;

        /*
         * Destination is the physical load address.
         *
         * NeoBench currently uses p_paddr for loading.
         */
        void *dst =
            (void *)(uintptr_t)ph[i].p_paddr;

        /*
         * Copy the file-backed portion.
         */
        memcpy(
            dst,
            src,
            ph[i].p_filesz);

        /*
         * Clear the memory-backed portion.
         *
         * This handles .bss and any zero-filled
         * tail of a loadable segment.
         */
        if (ph[i].p_memsz > ph[i].p_filesz)
        {
            memset(
                (uint8_t *)dst + ph[i].p_filesz,
                0,
                ph[i].p_memsz - ph[i].p_filesz);
        }
    }

    return 1;
}
