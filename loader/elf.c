#include "../MINIMAL"
#include <string.h>

typedef void (*entry_t)(void);

int elf_load(void* image, void** entry_out)
{
    elf_header_t* eh = (elf_header_t*)image;

    if (eh->magic != ELF_MAGIC)
        return -1;

    elf_program_header_t* ph =
        (elf_program_header_t*)((uint8_t*)image + eh->phoff);

    for (int i = 0; i < eh->phnum; i++)
    {
        if (ph[i].type != 1) continue; // LOAD segment

        void* src = (uint8_t*)image + ph[i].offset;
        void* dst = (void*)ph[i].paddr;

        memcpy(dst, src, ph[i].filesz);

        if (ph[i].memsz > ph[i].filesz)
        {
            memset((uint8_t*)dst + ph[i].filesz,
                   0,
                   ph[i].memsz - ph[i].filesz);
        }
    }

    *entry_out = (void*)eh->entry;
    return 0;
}
