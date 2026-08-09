/*
 * inode.c
 * NeoBench libNBFS
 */

#include <stdio.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"
#include <nbfs/layout.h>


static uint64_t inode_offset(uint64_t inode)
{
    return
        ((uint64_t)NBFS_INODE_TABLE_BLOCK *
        NBFS_DEFAULT_BLOCK_SIZE)
        +
        ((inode - 1) *
        sizeof(nbfs_inode_t));
}



int nbfs_read_inode(
    nbfs_context_t *ctx,
    uint64_t inode,
    nbfs_inode_t *out)
{
    if (!ctx || !out || inode == 0)
        return -1;


    if (fseek(ctx->image,
              inode_offset(inode),
              SEEK_SET))
        return -1;


    if (fread(out,
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
    if (!ctx || !inode)
        return -1;


    if (inode->inode_number == 0)
        return -1;


    if (fseek(ctx->image,
              inode_offset(inode->inode_number),
              SEEK_SET))
        return -1;


    if (fwrite(inode,
               sizeof(nbfs_inode_t),
               1,
               ctx->image) != 1)
        return -1;


    ctx->dirty = true;

    return 0;
}
