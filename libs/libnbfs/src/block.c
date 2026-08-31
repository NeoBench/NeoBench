#include <stdint.h>
#include <stdio.h>

#include "context_internal.h"

int nbfs_read_block(
    nbfs_context_t *ctx,
    uint64_t block,
    void *buffer)
{
    if (!ctx || !ctx->image || !buffer)
        return -1;

    uint32_t block_size = ctx->block_size;

    if (block_size == 0)
        block_size = NBFS_DEFAULT_BLOCK_SIZE;

    if (fseek(ctx->image,
              block * block_size,
              SEEK_SET))
        return -1;

    return fread(buffer,
                 block_size,
                 1,
                 ctx->image) == 1 ? 0 : -1;
}

int nbfs_write_block(
    nbfs_context_t *ctx,
    uint64_t block,
    const void *buffer)
{
    if (!ctx || !ctx->image || !buffer)
        return -1;

    uint32_t block_size = ctx->block_size;

    if (block_size == 0)
        block_size = NBFS_DEFAULT_BLOCK_SIZE;

    if (fseek(ctx->image,
              block * block_size,
              SEEK_SET))
        return -1;

    return fwrite(buffer,
                  block_size,
                  1,
                  ctx->image) == 1 ? 0 : -1;
}
