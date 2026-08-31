#!/bin/sh
#
# build_rootfs.sh
#
# Builds the small NBFS root image embedded into the kernel.
# The kernel boot path remounts this image onto a memory-backed
# block device, so the image must be small enough to embed.
#
# Usage: scripts/build_rootfs.sh
#

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

OUT_DIR="$ROOT/rootfs/boot"
IMAGE="$OUT_DIR/neobench-root.nbfs"
SIZE_MB="${NBFS_ROOT_SIZE_MB:-8}"

MKFS="$ROOT/tools/nbfs/mkfs/build/bin/mkfs.nbfs"
MKROOT="$ROOT/tools/nbfs/rootimg/build/bin/mkroot"

echo "==> Building root filesystem tools"
make -s -C "$ROOT/tools/nbfs/mkfs"

if [ ! -x "$MKROOT" ]; then
    make -s -C "$ROOT/tools/nbfs/rootimg"
fi

mkdir -p "$OUT_DIR"

echo "==> Formatting $IMAGE (${SIZE_MB} MB)"
rm -f "$IMAGE"
"$MKFS" "$IMAGE" "$SIZE_MB"

echo "==> Populating root filesystem"
"$MKROOT" "$IMAGE"

echo "==> Root image ready: $IMAGE"
ls -la "$IMAGE"