#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="tools/nbfs/mkfs"

mkdir -p "$ROOT/include/fs"

headers=(
types.h
nbfs.h
crc32.h
)

for f in "${headers[@]}"; do
    [[ -f "$ROOT/include/$f" ]] || cat > "$ROOT/include/$f" <<EOF
#ifndef ${f^^}
#define ${f^^}

/*
 * $f
 */

#endif
EOF
done

fs_headers=(
superblock.h
inode.h
directory.h
bitmap.h
journal.h
bootblock.h
)

for f in "${fs_headers[@]}"; do
    [[ -f "$ROOT/include/fs/$f" ]] || cat > "$ROOT/include/fs/$f" <<EOF
#ifndef NBFS_$(echo "$f" | tr '[:lower:].' '[:upper:]_' )
#define NBFS_$(echo "$f" | tr '[:lower:].' '[:upper:]_' )

/*
 * $f
 */

#endif
EOF
done

echo "NBFS header skeleton created."
