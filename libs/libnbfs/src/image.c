#include <stdint.h>
/*
 * NeoBench Filesystem Library
 *
 * image.c
 *
 * Image management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libnbfs.h"
#include "internal/context.h"

static uint64_t image_size(FILE *fp)
{
    long current = ftell(fp);

    fseek(fp, 0, SEEK_END);

    long size = ftell(fp);

    fseek(fp, current, SEEK_SET);

    return (uint64_t)size;
}

nbfs_context_t *nbfs_create(const char *path)
{
    nbfs_context_t *ctx = nbfs_context_create();

    if (!ctx)
        return NULL;

    ctx->image = fopen(path, "wb+");

    if (!ctx->image)
    {
        nbfs_context_destroy(ctx);
        return NULL;
    }

    strncpy(ctx->image_name,
            path,
            sizeof(ctx->image_name)-1);

    ctx->image_size = 0;
    ctx->block_size = NBFS_DEFAULT_BLOCK_SIZE;
    ctx->total_blocks = 0;
    ctx->dirty = true;

    return ctx;
}

nbfs_context_t *nbfs_open(const char *path)
{
    nbfs_context_t *ctx = nbfs_context_create();

    if (!ctx)
        return NULL;

    ctx->image = fopen(path, "rb+");

    if (!ctx->image)
    {
        nbfs_context_destroy(ctx);
        return NULL;
    }

    strncpy(ctx->image_name,
            path,
            sizeof(ctx->image_name)-1);

    ctx->image_size = image_size(ctx->image);

    ctx->block_size = NBFS_DEFAULT_BLOCK_SIZE;

    ctx->total_blocks =
        ctx->image_size / ctx->block_size;

    ctx->dirty = false;

    return ctx;
}

void nbfs_close(nbfs_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->dirty)
        fflush(ctx->image);

    nbfs_context_destroy(ctx);
}

int nbfs_flush(nbfs_context_t *ctx)
{
    if (!ctx)
        return -1;

    if (!ctx->image)
        return -1;

    fflush(ctx->image);

    ctx->dirty = false;

    return 0;
}
