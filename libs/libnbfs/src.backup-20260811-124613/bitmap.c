/*
 * bitmap.c
 * NeoBench libNBFS
 *
 * On-disk block/inode bitmap allocation.
 */

#include <stdint.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"
#include <nbfs/layout.h>

#define NBFS_BITMAP_BYTES NBFS_DEFAULT_BLOCK_SIZE
#define NBFS_INVALID_ID   UINT64_MAX

static int bitmap_test(
    const uint8_t *bitmap,
    uint64_t bit)
{
    return
        (bitmap[bit / 8] >>
         (bit % 8)) & 1u;
}

static void bitmap_set(
    uint8_t *bitmap,
    uint64_t bit)
{
    bitmap[bit / 8] |=
        (uint8_t)(1u << (bit % 8));
}

static void bitmap_clear(
    uint8_t *bitmap,
    uint64_t bit)
{
    bitmap[bit / 8] &=
        (uint8_t)~(1u << (bit % 8));
}

static uint64_t bitmap_find_zero(
    const uint8_t *bitmap,
    uint64_t first,
    uint64_t limit)
{
    for (uint64_t bit = first; bit < limit; bit++)
    {
        if (!bitmap_test(bitmap, bit))
            return bit;
    }

    return NBFS_INVALID_ID;
}

/*
 * Allocate a data block from the on-disk block bitmap.
 */
int nbfs_allocate_block(
    nbfs_context_t *ctx,
    uint64_t *block)
{
    uint8_t bitmap[NBFS_BITMAP_BYTES];
    nbfs_superblock_t sb;

    if (!ctx || !block)
        return -1;

    if (nbfs_read_superblock(ctx, &sb) != 0)
        return -1;

    if (sb.block_size != NBFS_DEFAULT_BLOCK_SIZE)
        return -1;

    if (sb.free_blocks == 0)
        return -1;

    if (nbfs_read_block(
            ctx,
            sb.block_bitmap_start,
            bitmap) != 0)
    {
        return -1;
    }

    uint64_t found =
        bitmap_find_zero(
            bitmap,
            sb.data_start,
            sb.total_blocks);

    if (found == NBFS_INVALID_ID)
        return -1;

    bitmap_set(bitmap, found);

    if (nbfs_write_block(
            ctx,
            sb.block_bitmap_start,
            bitmap) != 0)
    {
        return -1;
    }

    sb.free_blocks--;

    if (nbfs_write_superblock(ctx, &sb) != 0)
    {
        /*
         * Roll back the bitmap if the superblock
         * update fails.
         */
        bitmap_clear(bitmap, found);

        (void)nbfs_write_block(
            ctx,
            sb.block_bitmap_start,
            bitmap);

        return -1;
    }

    *block = found;

    return 0;
}

/*
 * Free a previously allocated data block.
 */
int nbfs_free_block(
    nbfs_context_t *ctx,
    uint64_t block)
{
    uint8_t bitmap[NBFS_BITMAP_BYTES];
    nbfs_superblock_t sb;

    if (!ctx)
        return -1;

    if (nbfs_read_superblock(ctx, &sb) != 0)
        return -1;

    if (block < sb.data_start ||
        block >= sb.total_blocks)
    {
        return -1;
    }

    if (nbfs_read_block(
            ctx,
            sb.block_bitmap_start,
            bitmap) != 0)
    {
        return -1;
    }

    if (!bitmap_test(bitmap, block))
        return -1;

    bitmap_clear(bitmap, block);

    if (nbfs_write_block(
            ctx,
            sb.block_bitmap_start,
            bitmap) != 0)
    {
        return -1;
    }

    sb.free_blocks++;

    if (sb.free_blocks > sb.total_blocks - sb.data_start)
        sb.free_blocks =
            sb.total_blocks - sb.data_start;

    if (nbfs_write_superblock(ctx, &sb) != 0)
        return -1;

    return 0;
}

/*
 * Allocate an inode from the inode bitmap.
 *
 * Inode 1 is the root inode and is already allocated.
 */
int nbfs_allocate_inode(
    nbfs_context_t *ctx,
    uint64_t *inode)
{
    uint8_t bitmap[NBFS_BITMAP_BYTES];
    nbfs_superblock_t sb;

    if (!ctx || !inode)
        return -1;

    if (nbfs_read_superblock(ctx, &sb) != 0)
        return -1;

    if (sb.block_size != NBFS_DEFAULT_BLOCK_SIZE)
        return -1;

    if (sb.total_inodes < 2)
        return -1;

    if (sb.free_inodes == 0)
        return -1;

    if (nbfs_read_block(
            ctx,
            sb.inode_bitmap_start,
            bitmap) != 0)
        return -1;

    /*
     * Inode numbers are one-based.
     *
     * inode 1 -> bitmap bit 0
     * inode 2 -> bitmap bit 1
     * inode 3 -> bitmap bit 2
     *
     * Inode 1 is the root inode and is permanently allocated.
     */
    uint64_t bit = bitmap_find_zero(
        bitmap,
        1,
        sb.total_inodes);

    if (bit == NBFS_INVALID_ID)
        return -1;

    bitmap_set(bitmap, bit);

    if (nbfs_write_block(
            ctx,
            sb.inode_bitmap_start,
            bitmap) != 0)
        return -1;

    sb.free_inodes--;

    if (nbfs_write_superblock(ctx, &sb) != 0)
    {
        bitmap_clear(bitmap, bit);

        (void)nbfs_write_block(
            ctx,
            sb.inode_bitmap_start,
            bitmap);

        return -1;
    }

    *inode = bit + 1;

    return 0;
}

int nbfs_free_inode(
    nbfs_context_t *ctx,
    uint64_t inode)
{
    uint8_t bitmap[NBFS_BITMAP_BYTES];
    nbfs_superblock_t sb;

    if (!ctx)
        return -1;

    if (nbfs_read_superblock(ctx, &sb) != 0)
        return -1;

    /*
     * Inode 1 is the root inode and cannot be freed.
     */
    if (inode < 2 || inode > sb.total_inodes)
        return -1;

    if (nbfs_read_block(
            ctx,
            sb.inode_bitmap_start,
            bitmap) != 0)
        return -1;

    /*
     * Convert one-based inode number to zero-based bitmap bit.
     *
     * inode 2 -> bit 1
     * inode 3 -> bit 2
     */
    uint64_t bit = inode - 1;

    if (!bitmap_test(bitmap, bit))
        return -1;

    bitmap_clear(bitmap, bit);

    if (nbfs_write_block(
            ctx,
            sb.inode_bitmap_start,
            bitmap) != 0)
        return -1;

    sb.free_inodes++;

    if (sb.free_inodes > sb.total_inodes - 1)
        sb.free_inodes = sb.total_inodes - 1;

    if (nbfs_write_superblock(ctx, &sb) != 0)
        return -1;

    return 0;
}
