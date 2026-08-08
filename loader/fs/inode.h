#ifndef NBFS_LOADER_INODE_H
#define NBFS_LOADER_INODE_H

#include <stdint.h>
#include <nbfs/inode.h>

int nbfs_read_inode(
    uint32_t inode_number,
    nbfs_inode_t *inode);

#endif
