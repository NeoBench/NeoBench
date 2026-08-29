/*
 * bitmap.c
 * NeoBench mkfs.nbfs
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/bitmap.h"
#include "mkfs.h"

static uint8_t bitmap[NBFS_DEFAULT_BLOCK_SIZE];

static uint64_t next_block = NBFS_DATA_START;

uint64_t nbfs_alloc_block(void)
{
    uint64_t block = next_block++;

    if (block >=
        (mkfs_image_size() /
         NBFS_DEFAULT_BLOCK_SIZE))
    {
        return UINT64_MAX;
    }

    bitmap[block / 8] |=
        (uint8_t)(1u << (block % 8));

    return block;
}

int nbfs_write_block_bitmap(FILE *fp)
{
    /*
     * Reserve every block before the data area:
     *
     * 0-323 = boot, superblock, bitmaps, inode table, journal.
     */
    for (uint64_t i = 0; i < NBFS_DATA_START; i++)
    {
        bitmap[i / 8] |=
            (uint8_t)(1u << (i % 8));
    }

    if (fseek(
            fp,
            (long)((uint64_t)NBFS_BLOCK_BITMAP *
                   NBFS_DEFAULT_BLOCK_SIZE),
            SEEK_SET) != 0)
    {
        return -1;
    }

    if (fwrite(
            bitmap,
            sizeof(bitmap),
            1,
            fp) != 1)
    {
        return -1;
    }

    fflush(fp);

    return 0;
}
