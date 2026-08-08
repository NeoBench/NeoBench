#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

LIB="libs/libnbfs"

mkdir -p "$LIB/include/internal"
mkdir -p "$LIB/src"

###############################################################################
# context.h
###############################################################################

cat > "$LIB/include/internal/context.h" <<'EOF'
#ifndef LIBNBFS_CONTEXT_H
#define LIBNBFS_CONTEXT_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <nbfs/nbfs.h>

typedef struct nbfs_context
{
    FILE *image;

    char image_name[256];

    uint64_t image_size;

    uint32_t block_size;

    uint64_t total_blocks;

    bool read_only;

    bool dirty;

    nbfs_superblock_t superblock;

} nbfs_context_t;

#endif
EOF

###############################################################################
# context.c
###############################################################################

cat > "$LIB/src/context.c" <<'EOF'
#include <stdlib.h>
#include <string.h>

#include "internal/context.h"

nbfs_context_t *nbfs_context_create(void)
{
    return calloc(1, sizeof(nbfs_context_t));
}

void nbfs_context_destroy(nbfs_context_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->image)
        fclose(ctx->image);

    free(ctx);
}
EOF

echo "Context module created."
