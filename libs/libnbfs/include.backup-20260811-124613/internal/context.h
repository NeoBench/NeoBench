#ifndef LIBNBFS_CONTEXT_H
#define LIBNBFS_CONTEXT_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <nbfs/nbfs.h>

typedef struct nbfs_context
{
    FILE *image;

    char image_name[256];

    uint64_t image_size;

    uint32_t block_size;

    uint64_t total_blocks;

    bool read_only;

    bool dirty;

    nbfs_superblock_t superblock;

} nbfs_context_t;

#endif
