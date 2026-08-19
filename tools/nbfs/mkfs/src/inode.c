/*
 * inode.c
 * NeoBench mkfs.nbfs
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/inode.h"

static uint8_t inode_bitmap[NBFS_DEFAULT_BLOCK_SIZE];

static uint64_t next_inode = 1;

uint64_t nbfs_alloc_inode(void)
{
    uint64_t inode = next_inode++;

    if (inode == 0 || inode > 1024)
        return UINT64_MAX;

    /*
     * Inode numbers are one-based:
     *
     * inode 1 -> bitmap bit 0
     * inode 2 -> bitmap bit 1
     * inode 3 -> bitmap bit 2
     */
    uint64_t bit = inode - 1;

    inode_bitmap[bit / 8] |=
        (uint8_t)(1u << (bit % 8));

    return inode;
}

int nbfs_write_inode_bitmap(FILE *fp)
{
    /*
     * Inode zero is reserved/invalid.
     * Root inode is inode 1.
     */
    inode_bitmap[0] |= 1u;

    if (fseek(
            fp,
            (long)((uint64_t)NBFS_INODE_BITMAP *
                   NBFS_DEFAULT_BLOCK_SIZE),
            SEEK_SET) != 0)
    {
        return -1;
    }

    if (fwrite(
            inode_bitmap,
            sizeof(inode_bitmap),
            1,
            fp) != 1)
    {
        return -1;
    }

    fflush(fp);

    return 0;
}

int nbfs_write_inode(
    FILE *fp,
    uint64_t inode_number,
    const nbfs_inode_t *inode)
{
    if (inode_number == 0 || inode_number > 1024)
        return -1;

    uint64_t offset =
        ((uint64_t)NBFS_INODE_TABLE *
         NBFS_DEFAULT_BLOCK_SIZE) +
        ((inode_number - 1) *
         sizeof(nbfs_inode_t));

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
        return -1;

    if (fwrite(
            inode,
            sizeof(nbfs_inode_t),
            1,
            fp) != 1)
    {
        return -1;
    }

    fflush(fp);

    return 0;
}
