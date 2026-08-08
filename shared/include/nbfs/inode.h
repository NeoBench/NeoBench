#ifndef NBFS_INODE_H
#define NBFS_INODE_H

#include <stdint.h>

#define NBFS_INODE_FREE      0
#define NBFS_INODE_FILE      1
#define NBFS_INODE_DIRECTORY 2

#define NBFS_DIRECT_EXTENTS 12

typedef struct
{
    uint32_t first_block;
    uint32_t block_count;
} nbfs_extent_t;

typedef struct
{
    uint32_t inode;

    uint16_t type;
    uint16_t links;

    uint32_t size;

    uint32_t flags;

    nbfs_extent_t extents[NBFS_DIRECT_EXTENTS];

} nbfs_inode_t;

#endif
