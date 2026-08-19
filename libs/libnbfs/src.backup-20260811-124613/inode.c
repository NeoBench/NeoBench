/*
 * inode.c
 * NeoBench libNBFS
 *
 * NBFS v1 inode operations.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "libnbfs.h"
#include "internal/context.h"
#include <nbfs/nbfs.h>
#include <nbfs/layout.h>

static uint64_t inode_offset(uint64_t inode)
{
    return
        ((uint64_t)NBFS_INODE_TABLE *
         NBFS_DEFAULT_BLOCK_SIZE)
        +
        ((inode - 1) *
         sizeof(nbfs_inode_t));
}

static int inode_valid(uint64_t inode)
{
    /*
     * NBFS v1 has 64 inode-table blocks.
     *
     * Calculate the maximum number of complete inodes
     * that fit in the inode table rather than hard-coding
     * the inode count.
     */
    uint64_t table_bytes =
        (uint64_t)NBFS_INODE_TABLE_BLOCKS *
        NBFS_DEFAULT_BLOCK_SIZE;

    uint64_t max_inodes =
        table_bytes / sizeof(nbfs_inode_t);

    return inode >= 1 && inode <= max_inodes;
}

int nbfs_read_inode(
    nbfs_context_t *ctx,
    uint64_t inode,
    nbfs_inode_t *out)
{
    if (!ctx || !ctx->image || !out)
        return -1;

    if (!inode_valid(inode))
        return -1;

    if (fseek(
            ctx->image,
            (long)inode_offset(inode),
            SEEK_SET) != 0)
        return -1;

    if (fread(
            out,
            sizeof(nbfs_inode_t),
            1,
            ctx->image) != 1)
        return -1;

    return 0;
}

int nbfs_write_inode(
    nbfs_context_t *ctx,
    const nbfs_inode_t *inode)
{
    if (!ctx || !ctx->image || !inode)
        return -1;

    if (!inode_valid(inode->inode_number))
        return -1;

    if (inode->inode_number == 0)
        return -1;

    if (fseek(
            ctx->image,
            (long)inode_offset(inode->inode_number),
            SEEK_SET) != 0)
        return -1;

    if (fwrite(
            inode,
            sizeof(nbfs_inode_t),
            1,
            ctx->image) != 1)
        return -1;

    ctx->dirty = true;

    return 0;
}
