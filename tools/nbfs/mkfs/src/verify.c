/*
 * verify.c
 * NeoBench mkfs.nbfs
 *
 * NBFS layout v1 verifier.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "layout.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/verify.h"
#include "mkfs.h"

static int read_block_bitmap(FILE *fp, uint8_t *bitmap)
{
    uint64_t offset =
        (uint64_t)NBFS_BLOCK_BITMAP *
        NBFS_DEFAULT_BLOCK_SIZE;

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
        return -1;

    if (fread(
            bitmap,
            NBFS_DEFAULT_BLOCK_SIZE,
            1,
            fp) != 1)
        return -1;

    return 0;
}

static int bit_is_set(
    const uint8_t *bitmap,
    uint64_t block)
{
    return
        (bitmap[block / 8] &
         (uint8_t)(1u << (block % 8))) != 0;
}

static int read_superblock(
    FILE *fp,
    nbfs_superblock_t *sb)
{
    uint64_t offset =
        (uint64_t)NBFS_SUPERBLOCK *
        NBFS_DEFAULT_BLOCK_SIZE;

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
        return -1;

    if (fread(sb, sizeof(*sb), 1, fp) != 1)
        return -1;

    return 0;
}

static int read_root_inode(
    FILE *fp,
    nbfs_inode_t *inode)
{
    uint64_t offset =
        ((uint64_t)NBFS_INODE_TABLE *
         NBFS_DEFAULT_BLOCK_SIZE);

    if (fseek(fp, (long)offset, SEEK_SET) != 0)
        return -1;

    if (fread(inode, sizeof(*inode), 1, fp) != 1)
        return -1;

    return 0;
}

int nbfs_verify_image(FILE *fp)
{
    nbfs_superblock_t sb;
    nbfs_inode_t root;
    uint8_t bitmap[NBFS_DEFAULT_BLOCK_SIZE];

    puts("NBFS filesystem verification");
    puts("============================");

    /*
     * Superblock
     */
    if (read_superblock(fp, &sb) != 0)
    {
        puts("FAIL: unable to read superblock.");
        return -1;
    }

    if (sb.magic != NBFS_MAGIC)
    {
        puts("FAIL: invalid NBFS magic.");
        return -1;
    }

    if (sb.version_major != NBFS_VERSION_MAJOR ||
        sb.version_minor != NBFS_VERSION_MINOR)
    {
        puts("FAIL: unsupported NBFS version.");
        return -1;
    }

    if (sb.block_size != NBFS_DEFAULT_BLOCK_SIZE)
    {
        puts("FAIL: invalid block size.");
        return -1;
    }

    if (sb.total_blocks !=
        mkfs_image_size() / NBFS_DEFAULT_BLOCK_SIZE)
    {
        puts("FAIL: invalid total block count.");
        return -1;
    }

    if (sb.root_inode != 1)
    {
        puts("FAIL: root inode is not inode 1.");
        return -1;
    }

    if (sb.journal_start != NBFS_JOURNAL_START ||
        sb.journal_blocks != NBFS_JOURNAL_BLOCKS)
    {
        puts("FAIL: invalid journal layout.");
        return -1;
    }

    if (sb.block_bitmap_start != NBFS_BLOCK_BITMAP ||
        sb.inode_bitmap_start != NBFS_INODE_BITMAP ||
        sb.inode_table_start != NBFS_INODE_TABLE ||
        sb.data_start != NBFS_DATA_START)
    {
        puts("FAIL: invalid filesystem layout.");
        return -1;
    }

    puts("PASS: superblock");

    /*
     * Block bitmap
     */
    if (read_block_bitmap(fp, bitmap) != 0)
    {
        puts("FAIL: unable to read block bitmap.");
        return -1;
    }

    for (uint64_t block = 0;
         block < NBFS_DATA_START;
         block++)
    {
        if (!bit_is_set(bitmap, block))
        {
            printf(
                "FAIL: reserved block %llu is free.\n",
                (unsigned long long)block);

            return -1;
        }
    }

    /*
     * Root directory must occupy the first data block.
     */
    if (!bit_is_set(bitmap, NBFS_DATA_START))
    {
        puts("FAIL: root directory block is not allocated.");
        return -1;
    }

    /*
     * Count free blocks from the bitmap.
     */
    uint64_t free_blocks = 0;

    for (uint64_t block = 0;
         block < mkfs_image_size() / NBFS_DEFAULT_BLOCK_SIZE;
         block++)
    {
        if (!bit_is_set(bitmap, block))
            free_blocks++;
    }

    if (free_blocks != sb.free_blocks)
    {
        printf(
            "FAIL: free block count mismatch "
            "(superblock=%llu bitmap=%llu).\n",
            (unsigned long long)sb.free_blocks,
            (unsigned long long)free_blocks);

        return -1;
    }

    puts("PASS: block bitmap");

    /*
     * Root inode
     */
    if (read_root_inode(fp, &root) != 0)
    {
        puts("FAIL: unable to read root inode.");
        return -1;
    }

    if (root.inode_number != 1)
    {
        puts("FAIL: root inode number is invalid.");
        return -1;
    }

    if (root.mode != 0x4000)
    {
        puts("FAIL: root inode is not a directory.");
        return -1;
    }

    if (root.size != NBFS_DEFAULT_BLOCK_SIZE)
    {
        puts("FAIL: invalid root directory size.");
        return -1;
    }

    if (root.extents[0].start_block != NBFS_DATA_START)
    {
        puts("FAIL: root directory is not at DATA_START.");
        return -1;
    }

    if (root.extents[0].block_count != 1)
    {
        puts("FAIL: invalid root directory extent.");
        return -1;
    }

    puts("PASS: root inode");

    /*
     * Verify the image size.
     */
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        puts("FAIL: unable to seek image.");
        return -1;
    }

    long image_size = ftell(fp);

    if (image_size != (long)mkfs_image_size())
    {
        puts("FAIL: invalid image size.");
        return -1;
    }

    puts("PASS: image size");

    puts("");
    puts("NBFS verification: OK");

    return 0;
}
