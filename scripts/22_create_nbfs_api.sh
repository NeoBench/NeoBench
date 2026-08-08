#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Find NeoBench root
###############################################################################

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

API="include/nbfs"

echo "========================================"
echo " Creating NBFS Public API"
echo "========================================"

mkdir -p "$API"

###############################################################################
# Public headers
###############################################################################

HEADERS=(
nbfs.h
types.h
constants.h
bootblock.h
superblock.h
bitmap.h
inode.h
extent.h
directory.h
journal.h
crc32.h
image.h
error.h
)

for H in "${HEADERS[@]}"
do
    FILE="$API/$H"

    if [[ ! -f "$FILE" ]]; then
cat > "$FILE" <<EOF
#ifndef NBFS_$(echo "$H" | tr '[:lower:].' '[:upper:]_' )
#define NBFS_$(echo "$H" | tr '[:lower:].' '[:upper:]_' )

/*
 * NeoBench Filesystem
 * Public API
 *
 * File: $H
 */

#include <stdint.h>

#endif
EOF
    fi
done

###############################################################################
# Internal library headers
###############################################################################

mkdir -p libs/libnbfs/include/internal

INTERNAL=(
private.h
layout.h
block_cache.h
allocator.h
verify.h
)

for H in "${INTERNAL[@]}"
do
    touch "libs/libnbfs/include/internal/$H"
done

###############################################################################
# Documentation
###############################################################################

mkdir -p docs/nbfs

cat > docs/nbfs/API.md <<EOF
# NBFS Public API

The public filesystem API is located in:

    include/nbfs/

All user-space tools and the NeoBench kernel include
the same headers to ensure the on-disk format remains
consistent.
EOF

echo
echo "Public API created."

find include/nbfs -maxdepth 1 | sort
