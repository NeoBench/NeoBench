#ifndef NBFS_FS_BITMAP_H
#define NBFS_FS_BITMAP_H

#include <stdint.h>
#include <stdio.h>

uint64_t nbfs_alloc_block(void);

int nbfs_write_block_bitmap(FILE *fp);

#endif
