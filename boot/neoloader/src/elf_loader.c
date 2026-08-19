#include <stdint.h>
#include <stddef.h>

#include "elf.h"
#include "elf_loader.h"

static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) |
           ((uint16_t)p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void neo_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n--)
        *d++ = *s++;
}

static void neo_memset(void *dst, uint8_t value, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n--)
        *d++ = value;
}

int neo_elf_load(const void *image,
                 uint32_t image_size,
                 neo_elf_info_t *info)
{
    const uint8_t *p = (const uint8_t *)image;

    uint32_t e_entry;
    uint32_t e_phoff;
    uint16_t e_phentsize;
    uint16_t e_phnum;

    uint32_t image_start = 0xffffffffU;
    uint32_t image_end = 0;
    uint32_t load_count = 0;

    if (!image || image_size < 52)
        return NEO_ELF_BAD_HEADER;

    if (p[0] != 0x7f ||
        p[1] != 'E' ||
        p[2] != 'L' ||
        p[3] != 'F')
        return NEO_ELF_BAD_MAGIC;

    if (p[4] != 1)
        return NEO_ELF_BAD_CLASS;

    if (p[5] != 2)
        return NEO_ELF_BAD_ENDIAN;

    if (p[6] != 1)
        return NEO_ELF_BAD_HEADER;

    if (be16(p + 16) != 2)
        return NEO_ELF_BAD_TYPE;

    if (be16(p + 18) != 4)
        return NEO_ELF_BAD_MACHINE;

    e_entry     = be32(p + 24);
    e_phoff     = be32(p + 28);
    e_phentsize = be16(p + 42);
    e_phnum     = be16(p + 44);

    if (e_phentsize != 32)
        return NEO_ELF_BAD_HEADER;

    if (e_phnum == 0)
        return NEO_ELF_NO_LOAD;

    /*
     * ELF32 program-header table bounds.
     * e_phentsize is fixed at 32, so avoid 64-bit arithmetic:
     * this loader is freestanding and must not depend on libgcc.
     */
    if (e_phoff > image_size)
        return NEO_ELF_BAD_PROGRAM;

    if (e_phnum > (image_size - e_phoff) / e_phentsize)
        return NEO_ELF_BAD_PROGRAM;

    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t *ph =
            p + e_phoff + ((uint32_t)i * e_phentsize);

        uint32_t p_type   = be32(ph + 0);
        uint32_t p_offset = be32(ph + 4);
        uint32_t p_vaddr  = be32(ph + 8);
        uint32_t p_paddr  = be32(ph + 12);
        uint32_t p_filesz = be32(ph + 16);
        uint32_t p_memsz  = be32(ph + 20);

        uint32_t dest;

        if (p_type != 1)
            continue;

        if (p_memsz < p_filesz)
            return NEO_ELF_BAD_PROGRAM;

        if (p_offset > image_size)
            return NEO_ELF_BAD_PROGRAM;

        if (p_filesz > image_size - p_offset)
            return NEO_ELF_BAD_PROGRAM;

        dest = p_paddr ? p_paddr : p_vaddr;

        if (dest < image_start)
            image_start = dest;

        /*
         * Detect 32-bit address wraparound.
         */
        if (p_memsz > 0xffffffffU - dest)
            return NEO_ELF_BAD_PROGRAM;

        if (dest + p_memsz > image_end)
            image_end = dest + p_memsz;

        load_count++;
    }

    if (load_count == 0)
        return NEO_ELF_NO_LOAD;

    if (info) {
        info->entry = e_entry;
        info->image_start = image_start;
        info->image_end = image_end;
        info->load_count = load_count;
    }

    return NEO_ELF_OK;
}

int neo_elf_load_memory(const void *image,
                        uint32_t image_size,
                        neo_elf_info_t *info)
{
    const uint8_t *p = (const uint8_t *)image;

    int rc = neo_elf_load(image, image_size, info);

    if (rc != NEO_ELF_OK)
        return rc;

    uint32_t e_phoff = be32(p + 28);
    uint16_t e_phentsize = be16(p + 42);
    uint16_t e_phnum = be16(p + 44);

    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t *ph =
            p + e_phoff + ((uint32_t)i * e_phentsize);

        uint32_t p_type   = be32(ph + 0);
        uint32_t p_offset = be32(ph + 4);
        uint32_t p_vaddr  = be32(ph + 8);
        uint32_t p_paddr  = be32(ph + 12);
        uint32_t p_filesz = be32(ph + 16);
        uint32_t p_memsz  = be32(ph + 20);

        uint32_t dest;

        if (p_type != 1)
            continue;

        dest = p_paddr ? p_paddr : p_vaddr;

        /*
         * Copy initialized data/text.
         */
        if (p_filesz != 0)
            neo_memcpy(
                (void *)(uintptr_t)dest,
                p + p_offset,
                p_filesz
            );

        /*
         * Zero BSS / remaining memory.
         */
        if (p_memsz > p_filesz)
            neo_memset(
                (void *)(uintptr_t)(dest + p_filesz),
                0,
                p_memsz - p_filesz
            );
    }

    return NEO_ELF_OK;
}
