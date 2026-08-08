#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Find project root
###############################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="$SCRIPT_DIR"

while [[ "$PROJECT_ROOT" != "/" ]]; do
    if [[ -f "$PROJECT_ROOT/Makefile" || -d "$PROJECT_ROOT/tools" ]]; then
        break
    fi
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

cd "$PROJECT_ROOT"

ROOT="tools/nbfs/mkfs"

echo "======================================="
echo " Creating mkfs.nbfs v1"
echo "======================================="

###############################################################################
# nbfs.h
###############################################################################

cat > "$ROOT/include/nbfs.h" <<'EOF'
#ifndef NBFS_H
#define NBFS_H

#include <stdint.h>

#define NBFS_MAGIC 0x4E424653UL
#define NBFS_BLOCK_SIZE 4096

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
} nbfs_superblock_t;

#endif
EOF

###############################################################################
# image.h
###############################################################################

cat > "$ROOT/include/image.h" <<'EOF'
#ifndef IMAGE_H
#define IMAGE_H

#include <stdio.h>
#include <stdint.h>

FILE *image_create(const char *path,uint64_t bytes);

#endif
EOF

###############################################################################
# mkfs.h
###############################################################################

cat > "$ROOT/include/mkfs.h" <<'EOF'
#ifndef MKFS_H
#define MKFS_H

int mkfs_create(const char *image);

#endif
EOF

###############################################################################
# image.c
###############################################################################

cat > "$ROOT/src/image.c" <<'EOF'
#include <stdio.h>
#include <stdint.h>

FILE *image_create(const char *path,uint64_t bytes)
{
    FILE *fp=fopen(path,"wb+");

    if(!fp)
        return NULL;

    fseek(fp,bytes-1,SEEK_SET);
    fputc(0,fp);
    rewind(fp);

    return fp;
}
EOF

###############################################################################
# mkfs.c
###############################################################################

cat > "$ROOT/src/mkfs.c" <<'EOF'
#include <stdio.h>

#include "image.h"

int mkfs_create(const char *image)
{
    FILE *fp=image_create(image,128ULL*1024ULL*1024ULL);

    if(!fp)
    {
        puts("Unable to create image.");
        return 1;
    }

    fclose(fp);

    puts("Filesystem image created.");

    return 0;
}
EOF

###############################################################################
# main.c
###############################################################################

cat > "$ROOT/src/main.c" <<'EOF'
#include <stdio.h>

#include "mkfs.h"

int main(int argc,char **argv)
{
    if(argc<2)
    {
        printf("Usage:\n");
        printf("  mkfs.nbfs disk.nbfs\n");
        return 1;
    }

    return mkfs_create(argv[1]);
}
EOF

echo
echo "======================================="
echo " mkfs.nbfs v1 Installed"
echo "======================================="
