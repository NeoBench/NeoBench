#ifndef NB_NBFS_H
#define NB_NBFS_H

#include <stdint.h>
#include <nbfs/superblock.h>

int nbfs_mount(void);

const nbfs_superblock_t *nbfs_superblock(void);

#endif
