#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NBFS Filesystem"
echo "========================================"

mkdir -p kernel/fs/nbfs
mkdir -p include/kernel/nbfs

touch kernel/fs/nbfs/superblock.c
touch kernel/fs/nbfs/inode.c
touch kernel/fs/nbfs/directory.c
touch kernel/fs/nbfs/file.c
touch kernel/fs/nbfs/extent.c
touch kernel/fs/nbfs/bitmap.c
touch kernel/fs/nbfs/journal.c
touch kernel/fs/nbfs/cache.c
touch kernel/fs/nbfs/block.c
touch kernel/fs/nbfs/format.c
touch kernel/fs/nbfs/mount.c
touch kernel/fs/nbfs/unmount.c

touch include/kernel/nbfs/superblock.h
touch include/kernel/nbfs/inode.h
touch include/kernel/nbfs/directory.h
touch include/kernel/nbfs/file.h
touch include/kernel/nbfs/extent.h
touch include/kernel/nbfs/bitmap.h
touch include/kernel/nbfs/journal.h
touch include/kernel/nbfs/cache.h
touch include/kernel/nbfs/block.h
touch include/kernel/nbfs/format.h

cat > kernel/fs/nbfs/README.md <<EOF
NeoBench File System (NBFS)

Modules

- Superblock
- Inodes
- Directories
- Extents
- Block Allocator
- Bitmap Manager
- Journal
- Cache
- Formatter
- Mount/Unmount
EOF

echo
echo "NBFS source tree created."
