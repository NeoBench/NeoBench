#ifndef NBFS_FS_INODE_H
#define NBFS_FS_INODE_H

#include <stdio.h>
#include <stdint.h>

#include <nbfs/nbfs.h>

uint64_t nbfs_alloc_inode(void);

int nbfs_write_inode(
    FILE *fp,
    uint64_t inode_number,
    const nbfs_inode_t *inode
);

int nbfs_write_inode_bitmap(FILE *fp);

int nbfs_create_root_inode(FILE *fp);

#endif
