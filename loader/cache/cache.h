#ifndef NB_CACHE_H
#define NB_CACHE_H

#include <stdint.h>

#define CACHE_BLOCKS 16

int cache_init(void);

int cache_read(
    uint32_t block,
    void *buffer);

#endif
