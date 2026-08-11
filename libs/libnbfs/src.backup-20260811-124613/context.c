#include "context_internal.h"
#include <stdlib.h>
#include <string.h>

nbfs_context_t *nbfs_context_create(void)
{
    nbfs_context_t *ctx = calloc(1, sizeof(*ctx));

    if (!ctx)
        return NULL;

    return ctx;
}

void nbfs_context_destroy(nbfs_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->image)
        fclose(ctx->image);

    free(ctx);
}
