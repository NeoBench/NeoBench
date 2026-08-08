#ifndef NBFS_SUPERBLOCK_H
#define NBFS_SUPERBLOCK_H

#include <stdint.h>

#define NBFS_MAGIC 0x4E424653U /* "NBFS" */
#define NBFS_VERSION 1

typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t block_size;

    uint32_t total_blocks;

    uint32_t inode_table;

    uint32_t bitmap_start;

    uint32_t root_inode;

    uint32_t journal_start;

    uint32_t journal_blocks;

    uint32_t checksum;

} nbfs_superblock_t;

#endif
