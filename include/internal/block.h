#ifndef LIBNBFS_INTERNAL_BLOCK_H
#define LIBNBFS_INTERNAL_BLOCK_H

#include <stdint.h>

#include "context.h"

int nbfs_block_read(
    nbfs_context_t *ctx,
    uint64_t block,
    void *buffer);

int nbfs_block_write(
    nbfs_context_t *ctx,
    uint64_t block,
    const void *buffer);

#endif
