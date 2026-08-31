#ifndef NBFH_VOL_H
#define NBFH_VOL_H

#include <stdint.h>
#include "fmt.h"

struct nbh_io
{
    void *ud;
    int (*read)(void *ud, uint32_t sector, uint32_t count, void *buf);
};

struct nbh_ent
{
    uint8_t type;      /* NBH_DIRENTRY_DIR / NBH_DIRENTRY_FILE */
    uint32_t inode;
};

struct nbh_vol
{
    struct nbh_io io;
    uint32_t sector_size;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t root_inode;
    uint32_t itable_start;
    uint32_t inode_blocks;
    uint32_t data_start;
    char volname[64];
    uint8_t inode_buf[NBH_BLOCK_SIZE];
};

int  nbh_mount(struct nbh_vol *v);
int  nbh_read_inode(struct nbh_vol *v, uint32_t inode, struct nbh_inode *out);
int  nbh_read(struct nbh_vol *v, const struct nbh_inode *e,
              uint64_t offset, void *buf, uint32_t size);
int  nbh_direnum(struct nbh_vol *v, uint32_t dir_inode,
                 uint32_t ordinal, struct nbh_ent *out, char *name, uint32_t name_size);
int  nbh_dirfind(struct nbh_vol *v, uint32_t dir_inode,
                 const char *name, struct nbh_ent *out);
int  nbh_volstat(struct nbh_vol *v, uint32_t *num_blocks,
                 uint32_t *num_used, uint32_t *bytes_per_block);

#endif