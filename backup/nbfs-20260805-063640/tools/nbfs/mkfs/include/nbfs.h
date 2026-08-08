#ifndef NBFS_H
#define NBFS_H

#include <stdint.h>

#define NBFS_MAGIC 0x4E424653UL
#define NBFS_BLOCK_SIZE 4096

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
} nbfs_superblock_t;

#endif
