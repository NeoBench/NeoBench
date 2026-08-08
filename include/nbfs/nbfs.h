#ifndef NBFS_NBFS_H
#define NBFS_NBFS_H

#include <stdint.h>

#ifdef __GNUC__
#define NBFS_PACKED __attribute__((packed))
#else
#define NBFS_PACKED
#endif

/* -------------------------------------------------------------------------
 * Filesystem constants
 * ------------------------------------------------------------------------- */

#define NBFS_MAGIC          0x5346424Eu  /* "NBFS" */
#define NBFS_VERSION_MAJOR  1
#define NBFS_VERSION_MINOR  0

#define NBFS_BLOCK_SIZE_1K   1024
#define NBFS_BLOCK_SIZE_2K   2048
#define NBFS_BLOCK_SIZE_4K   4096
#define NBFS_BLOCK_SIZE_8K   8192
#define NBFS_BLOCK_SIZE_16K 16384

#define NBFS_DEFAULT_BLOCK_SIZE NBFS_BLOCK_SIZE_4K

/* -------------------------------------------------------------------------
 * Fixed block locations - layout v1
 *
 * 0          boot block
 * 1          superblock
 * 2          block bitmap
 * 3          inode bitmap
 * 4-67       inode table
 * 68-323     journal
 * 324+       filesystem data
 * ------------------------------------------------------------------------- */

#define NBFS_BOOT_BLOCK          0
#define NBFS_SUPERBLOCK          1
#define NBFS_BLOCK_BITMAP        2
#define NBFS_INODE_BITMAP        3
#define NBFS_INODE_TABLE         4
#define NBFS_INODE_TABLE_BLOCKS  64

#define NBFS_JOURNAL_START \
    (NBFS_INODE_TABLE + NBFS_INODE_TABLE_BLOCKS)

#define NBFS_JOURNAL_BLOCKS      256

#define NBFS_DATA_START \
    (NBFS_JOURNAL_START + NBFS_JOURNAL_BLOCKS)

/* -------------------------------------------------------------------------
 * Superblock
 * ------------------------------------------------------------------------- */

typedef struct NBFS_PACKED
{
    uint32_t magic;

    uint16_t version_major;
    uint16_t version_minor;

    uint32_t block_size;
    uint32_t flags;

    uint64_t total_blocks;
    uint64_t free_blocks;

    uint64_t total_inodes;
    uint64_t free_inodes;

    uint64_t root_inode;

    uint64_t journal_start;
    uint64_t journal_blocks;

    uint64_t block_bitmap_start;
    uint64_t inode_bitmap_start;
    uint64_t inode_table_start;
    uint64_t data_start;

    char volume_name[64];

    uint32_t crc32;

    uint8_t reserved[128];

} nbfs_superblock_t;

/* -------------------------------------------------------------------------
 * Extent
 * ------------------------------------------------------------------------- */

typedef struct NBFS_PACKED
{
    uint64_t start_block;
    uint32_t block_count;
    uint32_t flags;

} nbfs_extent_t;

/* -------------------------------------------------------------------------
 * Inode
 * ------------------------------------------------------------------------- */

#define NBFS_EXTENTS_PER_INODE 12

typedef struct NBFS_PACKED
{
    uint64_t inode_number;

    uint16_t mode;
    uint16_t links;

    uint32_t uid;
    uint32_t gid;

    uint64_t size;

    uint64_t created;
    uint64_t modified;
    uint64_t accessed;

    nbfs_extent_t extents[NBFS_EXTENTS_PER_INODE];

    uint32_t crc32;

} nbfs_inode_t;

/* -------------------------------------------------------------------------
 * Directory entry
 * ------------------------------------------------------------------------- */

typedef struct NBFS_PACKED
{
    uint64_t inode;

    uint16_t record_length;
    uint8_t  name_length;
    uint8_t  type;

    /* filename bytes follow */

} nbfs_directory_entry_t;

#endif /* NBFS_NBFS_H */
