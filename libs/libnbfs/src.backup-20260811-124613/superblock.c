/*
 * NeoBench Filesystem Library
 *
 * superblock.c
 *
 * Superblock management
 */

#include <string.h>
#include <stdint.h>

#include "../include/libnbfs.h"
#include "context_internal.h"


int nbfs_read_superblock(
    nbfs_context_t *ctx,
    nbfs_superblock_t *sb)
{
    if (!ctx || !sb)
        return -1;

    /*
     * The block layer reads a full filesystem block.
     * Do not read directly into the smaller superblock structure.
     */
    uint8_t buffer[NBFS_DEFAULT_BLOCK_SIZE];

    if (nbfs_read_block(ctx,
                        NBFS_SUPERBLOCK,
                        buffer) != 0)
    {
        return -1;
    }

    memcpy(sb,
           buffer,
           sizeof(nbfs_superblock_t));

    /*
     * Cache the loaded superblock.
     */
    ctx->superblock = *sb;

    /*
     * Update runtime block information.
     */
    if (sb->block_size != 0)
        ctx->block_size = sb->block_size;

    ctx->total_blocks = sb->total_blocks;

    return 0;
}


int nbfs_write_superblock(
    nbfs_context_t *ctx,
    const nbfs_superblock_t *sb)
{
    if (!ctx || !sb)
        return -1;

    uint8_t buffer[NBFS_DEFAULT_BLOCK_SIZE];

    memset(buffer,
           0,
           sizeof(buffer));

    memcpy(buffer,
           sb,
           sizeof(nbfs_superblock_t));

    if (nbfs_write_block(ctx,
                         NBFS_SUPERBLOCK,
                         buffer) != 0)
    {
        return -1;
    }

    ctx->superblock = *sb;

    if (sb->block_size != 0)
        ctx->block_size = sb->block_size;

    ctx->total_blocks = sb->total_blocks;

    ctx->dirty = true;

    return 0;
}


int nbfs_verify_superblock(
    const nbfs_superblock_t *sb)
{
    if (!sb)
        return -1;


    if (sb->magic != NBFS_MAGIC)
        return -1;


    if (sb->version_major != NBFS_VERSION_MAJOR)
        return -1;


    if (sb->block_size != NBFS_BLOCK_SIZE_4K)
        return -1;


    if (sb->total_blocks == 0)
        return -1;


    return 0;
}
