#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Locate project root
###############################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

while [[ "$PROJECT_ROOT" != "/" ]]; do
    if [[ -d "$PROJECT_ROOT/kernel" ]]; then
        break
    fi
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

if [[ "$PROJECT_ROOT" == "/" ]]; then
    echo "NeoBench root not found."
    exit 1
fi

cd "$PROJECT_ROOT"

echo "========================================"
echo " Creating NBFS Repository Layout"
echo "========================================"

###############################################################################
# Shared include
###############################################################################

mkdir -p include/nbfs

###############################################################################
# Kernel
###############################################################################

mkdir -p kernel/fs/nbfs

###############################################################################
# Shared library
###############################################################################

mkdir -p libs/libnbfs/{src,include,build}

###############################################################################
# User tools
###############################################################################

TOOLS=(
mkfs.nbfs
fsck.nbfs
mount.nbfs
dump.nbfs
label.nbfs
tune.nbfs
nbfsinfo
)

for TOOL in "${TOOLS[@]}"; do
    mkdir -p "tools/$TOOL"/{src,include,tests,docs,build}
done

###############################################################################
# Documentation
###############################################################################

mkdir -p docs/nbfs

touch docs/nbfs/README.md
touch docs/nbfs/layout.md
touch docs/nbfs/superblock.md
touch docs/nbfs/inodes.md
touch docs/nbfs/extents.md
touch docs/nbfs/journal.md
touch docs/nbfs/directory.md
touch docs/nbfs/boot.md
touch docs/nbfs/api.md

###############################################################################
# Shared headers
###############################################################################

touch include/nbfs/nbfs.h
touch include/nbfs/types.h
touch include/nbfs/superblock.h
touch include/nbfs/inode.h
touch include/nbfs/extent.h
touch include/nbfs/directory.h
touch include/nbfs/bitmap.h
touch include/nbfs/journal.h
touch include/nbfs/crc32.h
touch include/nbfs/bootblock.h

###############################################################################
# Kernel stubs
###############################################################################

touch kernel/fs/nbfs/superblock.c
touch kernel/fs/nbfs/inode.c
touch kernel/fs/nbfs/directory.c
touch kernel/fs/nbfs/bitmap.c
touch kernel/fs/nbfs/journal.c
touch kernel/fs/nbfs/mount.c
touch kernel/fs/nbfs/file.c

###############################################################################
# Library stubs
###############################################################################

touch libs/libnbfs/src/libnbfs.c
touch libs/libnbfs/include/libnbfs.h

###############################################################################
# Summary
###############################################################################

echo
echo "NBFS repository layout created."
find include/nbfs kernel/fs/nbfs libs/libnbfs docs/nbfs tools -maxdepth 2 | sort
