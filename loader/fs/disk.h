#ifndef NB_DISK_H
#define NB_DISK_H

#include <stdint.h>

#define NBFS_BLOCK_SIZE 4096

int disk_init(void);

int disk_read_blocks(
    uint32_t block,
    uint32_t count,
    void *buffer);

#endif
