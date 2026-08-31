/*
 * superblock.c
 * NeoBench mkfs.nbfs
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nbfs/nbfs.h>

#include "fs/superblock.h"
#include "mkfs.h"

#define NBFS_TOTAL_INODES 1024ULL

int nbfs_write_superblock(FILE *fp)
{
    nbfs_superblock_t sb;

    memset(&sb, 0, sizeof(sb));

    sb.magic = NBFS_MAGIC;

    sb.version_major = NBFS_VERSION_MAJOR;
    sb.version_minor = NBFS_VERSION_MINOR;

    sb.block_size = NBFS_DEFAULT_BLOCK_SIZE;
    sb.flags = 0;

    sb.total_blocks =
        mkfs_image_size() / NBFS_DEFAULT_BLOCK_SIZE;

    /*
     * Blocks before DATA_START are permanently reserved:
     *
     * 0-323 = metadata + journal
     *
     * One data block is already allocated to the root directory.
     */
    sb.free_blocks =
        sb.total_blocks -
        NBFS_DATA_START -
        1;

    sb.total_inodes = NBFS_TOTAL_INODES;
    sb.free_inodes  = NBFS_TOTAL_INODES - 1;

    sb.root_inode = 1;

    sb.journal_start = NBFS_JOURNAL_START;
    sb.journal_blocks = NBFS_JOURNAL_BLOCKS;

    sb.block_bitmap_start = NBFS_BLOCK_BITMAP;
    sb.inode_bitmap_start = NBFS_INODE_BITMAP;
    sb.inode_table_start  = NBFS_INODE_TABLE;
    sb.data_start         = NBFS_DATA_START;

    strncpy(
        sb.volume_name,
        "NeoBench",
        sizeof(sb.volume_name) - 1);

    sb.crc32 = 0;

    if (fseek(
            fp,
            (long)((uint64_t)NBFS_SUPERBLOCK *
                   NBFS_DEFAULT_BLOCK_SIZE),
            SEEK_SET) != 0)
    {
        return -1;
    }

    if (fwrite(
            &sb,
            sizeof(sb),
            1,
            fp) != 1)
    {
        return -1;
    }

    fflush(fp);

    return 0;
}
