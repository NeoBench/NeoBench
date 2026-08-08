#!/usr/bin/env bash
#
# NeoBench NBFS Unification Tool
#
# Backs up duplicate headers, removes obsolete copies,
# updates include paths, and verifies the resulting tree.
#

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BACKUP="$ROOT/backup/nbfs-$(date +%Y%m%d-%H%M%S)"

echo "========================================="
echo " NeoBench NBFS Merge Tool"
echo "========================================="
echo

mkdir -p "$BACKUP"

CANONICAL="$ROOT/include/nbfs"

if [ ! -d "$CANONICAL" ]; then
    echo "ERROR: $CANONICAL not found."
    exit 1
fi

echo "[1/6] Backing up duplicate header trees..."

for DIR in \
    "$ROOT/tools/nbfs/mkfs/include" \
    "$ROOT/libs/libnbfs/include"
do
    if [ -d "$DIR" ]; then
        echo "  Backup: $DIR"
        mkdir -p "$BACKUP$(dirname "${DIR#$ROOT}")"
        cp -a "$DIR" "$BACKUP$(dirname "${DIR#$ROOT}")/"
    fi
done

echo
echo "[2/6] Removing obsolete NBFS header copies..."

rm -f "$ROOT/tools/nbfs/mkfs/include/"*.h 2>/dev/null || true
rm -f "$ROOT/libs/libnbfs/include/nbfs/"*.h 2>/dev/null || true

mkdir -p "$ROOT/tools/nbfs/mkfs/include"
mkdir -p "$ROOT/libs/libnbfs/include"

echo
echo "[3/6] Creating canonical include links..."

ln -sf ../../../include/nbfs "$ROOT/tools/nbfs/mkfs/include/nbfs"
ln -sf ../../include/nbfs "$ROOT/libs/libnbfs/include/nbfs"

echo
echo "[4/6] Updating source includes..."

find "$ROOT" \
    -type f \
    \( -name "*.c" -o -name "*.h" \) \
| while read -r file
do
    sed -i \
        -e 's|#include "nbfs\.h"|#include <nbfs/nbfs.h>|g' \
        -e 's|#include "superblock\.h"|#include <nbfs/superblock.h>|g' \
        -e 's|#include "inode\.h"|#include <nbfs/inode.h>|g' \
        -e 's|#include "directory\.h"|#include <nbfs/directory.h>|g' \
        -e 's|#include "extent\.h"|#include <nbfs/extent.h>|g' \
        -e 's|#include "journal\.h"|#include <nbfs/journal.h>|g' \
        -e 's|#include "bitmap\.h"|#include <nbfs/bitmap.h>|g' \
        "$file"
done

echo
echo "[5/6] Checking for duplicate filesystem headers..."

find "$ROOT" -name nbfs.h
find "$ROOT" -name superblock.h

echo
echo "[6/6] Rebuilding projects..."

(
cd "$ROOT/libs/libnbfs" &&
make clean &&
make
)

(
cd "$ROOT/loader" &&
make clean &&
make
)

(
cd "$ROOT/tools/image" &&
make
)

(
cd "$ROOT/tools/nbfs/mkfs" &&
make clean &&
make
)

echo
echo "========================================="
echo " NBFS merge complete"
echo "========================================="
echo
echo "Backup stored in:"
echo "  $BACKUP"
echo
echo "Next recommended step:"
echo "  Replace all duplicate filesystem structure"
echo "  definitions with the canonical"
echo "  include/nbfs tree."
