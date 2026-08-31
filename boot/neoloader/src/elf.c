#include "elf.h"
#include "loader.h"

static void neo_memcpy(void *dst, const void *src, uint32_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--)
        *d++ = *s++;
}

static void neo_memset(void *dst, unsigned char value, uint32_t n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n--)
        *d++ = value;
}

int neo_elf_load(
    const void *image,
    uint32_t image_size,
    neo_elf_info_t *info
)
{
    const Elf32_Ehdr *eh;
    const Elf32_Phdr *ph;
    uint32_t i;
    uint32_t image_start = 0xffffffffU;
    uint32_t image_end = 0;

    if (!image || !info)
        return NEOLOADER_BAD_ELF;

    if (image_size < sizeof(Elf32_Ehdr))
        return NEOLOADER_BAD_ELF;

    eh = (const Elf32_Ehdr *)image;

    if (eh->e_ident[0] != 0x7f ||
        eh->e_ident[1] != 'E'  ||
        eh->e_ident[2] != 'L'  ||
        eh->e_ident[3] != 'F')
        return NEOLOADER_BAD_ELF;

    if (eh->e_ident[4] != ELFCLASS32)
        return NEOLOADER_BAD_ELF;

    if (eh->e_ident[5] != ELFDATA2MSB)
        return NEOLOADER_BAD_ELF;

    if (eh->e_type != ET_EXEC)
        return NEOLOADER_BAD_ELF;

    if (eh->e_machine != EM_68K)
        return NEOLOADER_BAD_ARCH;

    if (eh->e_phentsize != sizeof(Elf32_Phdr))
        return NEOLOADER_BAD_ELF;

    if (eh->e_phoff > image_size)
        return NEOLOADER_BAD_ELF;

    if (eh->e_phnum >
        (image_size - eh->e_phoff) / sizeof(Elf32_Phdr))
        return NEOLOADER_BAD_ELF;

    ph = (const Elf32_Phdr *)
        ((const unsigned char *)image + eh->e_phoff);

    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t end;

        if (ph[i].p_type != PT_LOAD)
            continue;

        if (ph[i].p_filesz > ph[i].p_memsz)
            return NEOLOADER_BAD_LOAD;

        if (ph[i].p_offset > image_size)
            return NEOLOADER_BAD_LOAD;

        if (ph[i].p_filesz >
            image_size - ph[i].p_offset)
            return NEOLOADER_BAD_LOAD;

        end = ph[i].p_vaddr + ph[i].p_memsz;

        if (end < ph[i].p_vaddr)
            return NEOLOADER_BAD_LOAD;

        neo_memcpy(
            (void *)(uintptr_t)ph[i].p_vaddr,
            (const unsigned char *)image + ph[i].p_offset,
            ph[i].p_filesz
        );

        if (ph[i].p_memsz > ph[i].p_filesz) {
            neo_memset(
                (void *)(uintptr_t)
                    (ph[i].p_vaddr + ph[i].p_filesz),
                0,
                ph[i].p_memsz - ph[i].p_filesz
            );
        }

        if (ph[i].p_vaddr < image_start)
            image_start = ph[i].p_vaddr;

        if (end > image_end)
            image_end = end;
    }

    if (image_start == 0xffffffffU)
        return NEOLOADER_BAD_LOAD;

    info->entry = eh->e_entry;
    info->image_start = image_start;
    info->image_end = image_end;

    return NEOLOADER_OK;
}
