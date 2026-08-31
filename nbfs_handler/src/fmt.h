#ifndef NBFH_FMT_H
#define NBFH_FMT_H

#include <stdint.h>

#define NBH_SECTOR_SIZE          512u
#define NBH_BLOCK_SIZE           4096u
#define NBH_SECTORS_PER_BLOCK    8u

#define NBFS_MODE_DIRECTORY      0x4000u
#define NBFS_MODE_FILE           0x8000u

#define NBH_MAGIC                0x5346424Eu    /* "NBFS" LE */
#define NBH_VERSION              1u
#define NBH_BLOCK_PER_BITMAP     4096u
#define NBH_INODE_TABLE_BLOCKS   64u

#define NBH_FLAG_DIRTY           0x00000001u
#define NBH_FLAG_HAS_JOURNAL     0x00000002u

#define NBH_NBIDX_POS            4u
#define NBH_BMP_POS              8u
#define NBH_IBMP_POS             12u
#define NBH_ITABLE_POS           16u
#define NBH_DATA_POS             324u

#define NBH_DIRENTRY_FILE        1
#define NBH_DIRENTRY_DIR         2

struct nbh_super
{
    uint64_t magic;
    uint16_t vmaj;
    uint16_t vmin;
    uint16_t bs;
    uint16_t flags;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t root_inode;
    uint64_t journal_start;
    uint32_t jblocks;
    uint64_t bmp_start;
    uint64_t ibmp_start;
    uint64_t itable_start;
    uint64_t data_start;
    char volname[64];
    uint32_t crc;
    uint32_t crc_reserved;
    uint8_t reserved[128];
};

#define NBH_INODE_SIZE           248u
#define NBH_INODES_PER_BLOCK     (NBH_BLOCK_SIZE / NBH_INODE_SIZE)
#define NBH_MAX_INODES           (NBH_INODE_TABLE_BLOCKS * NBH_INODES_PER_BLOCK)

#define NBH_EXTENTS_PER_INODE    12u
#define NBH_EXT_FLAG_DIRECT      0x00000001u
#define NBH_EXT_FLAG_BLOCK       0x00000002u
#define NBH_EXT_BLOCK_INDEX      (NBH_EXTENTS_PER_INODE - 1)

struct nbh_extent
{
    uint64_t start_block;
    uint32_t block_count;
    uint32_t flags;
};

/* Mirrors kernel nbfs_inode_t (packed, little-endian on disk). */
struct nbh_inode
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
    struct nbh_extent extents[NBH_EXTENTS_PER_INODE];
    uint32_t crc;
};

#define NBH_INO_OFF_MODE       8u
#define NBH_INO_OFF_LINKS      10u
#define NBH_INO_OFF_UID        12u
#define NBH_INO_OFF_GID        16u
#define NBH_INO_OFF_SIZE       20u
#define NBH_INO_OFF_EXTENTS    52u
#define NBH_INO_EXTENT_STRIDE  16u

#define NBH_DIRENTRY_MAXNAME     108u

struct nbh_dirent
{
    uint64_t inode;
    uint16_t rec_len;
    uint8_t nlen;
    uint8_t type;
    char name[NBH_DIRENTRY_MAXNAME];
};

static inline uint16_t nbh_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t nbh_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t nbh_rd64(const uint8_t *p)
{
    return (uint64_t)nbh_rd32(p) | ((uint64_t)nbh_rd32(p + 4) << 32);
}

#endif