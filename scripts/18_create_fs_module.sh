#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Locate project root
###############################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

while [[ "$PROJECT_ROOT" != "/" ]]; do
    if [[ -d "$PROJECT_ROOT/tools" ]]; then
        break
    fi
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

if [[ "$PROJECT_ROOT" == "/" ]]; then
    echo "NeoBench project root not found."
    exit 1
fi

cd "$PROJECT_ROOT"

ROOT="tools/nbfs/mkfs"

echo "========================================"
echo " Creating NBFS Filesystem Module"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p "$ROOT/src/fs"
mkdir -p "$ROOT/include/fs"

###############################################################################
# Header files
###############################################################################

HEADERS=(
superblock.h
bootblock.h
bitmap.h
inode.h
directory.h
journal.h
)

for FILE in "${HEADERS[@]}"; do

TARGET="$ROOT/include/fs/$FILE"

if [[ ! -f "$TARGET" ]]; then

GUARD=$(echo "NBFS_FS_${FILE}" | tr '[:lower:].' '[:upper:]_')

cat > "$TARGET" <<EOF
#ifndef ${GUARD}
#define ${GUARD}

#include <stdio.h>
#include <stdint.h>

#endif
EOF

fi

done

###############################################################################
# Source files
###############################################################################

SOURCES=(
superblock.c
bootblock.c
bitmap.c
inode.c
directory.c
journal.c
)

for FILE in "${SOURCES[@]}"; do

TARGET="$ROOT/src/fs/$FILE"

if [[ ! -f "$TARGET" ]]; then

cat > "$TARGET" <<EOF
/*
 * $FILE
 * NBFS filesystem module
 */

#include <stdio.h>

EOF

fi

done

###############################################################################
# README
###############################################################################

cat > "$ROOT/src/fs/README.md" <<EOF
# NBFS Filesystem Module

Source files:

- bootblock.c
- superblock.c
- bitmap.c
- inode.c
- directory.c
- journal.c

These files contain the on-disk filesystem implementation used by mkfs.nbfs.
EOF

###############################################################################
# Display results
###############################################################################

echo
echo "Filesystem module created:"
find "$ROOT/src/fs" -maxdepth 1 | sort
echo
find "$ROOT/include/fs" -maxdepth 1 | sort
echo
echo "Done."

