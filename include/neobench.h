#ifndef NEOBENCH_H
#define NEOBENCH_H

#include "types.h"

#define NEOBENCH_VERSION_MAJOR 1
#define NEOBENCH_VERSION_MINOR 0
#define NEOBENCH_VERSION_PATCH 0
#define NEOBENCH_CODENAME "Denise"

#define INODE_SIZE 256
#define MAX_INLINE_DATA 128
#define MAX_INLINE_EXTENTS 6

typedef struct {
    uint32_t logical_block;
    uint32_t physical_block;
    uint16_t length;
    uint16_t flags;
} NB_Extent;

typedef struct {
    uint16_t magic;
    uint16_t entries;
    uint16_t max_entries;
    uint16_t depth;
} NB_ExtentHeader;

typedef struct {
    uint16_t flags;
    uint16_t uid;
    uint16_t gid;
    uint16_t link_count;
    uint32_t size_lo;
    uint32_t size_hi;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint32_t crtime;
    uint32_t block_count;
    uint32_t xattr_block;
    uint32_t generation;
    uint32_t crc32;
    union {
        struct {
            NB_ExtentHeader header;
            NB_Extent       extents[MAX_INLINE_EXTENTS];
        } tree;
        uint8_t inline_data[MAX_INLINE_DATA];
    } data;
} NB_Inode;

#endif
