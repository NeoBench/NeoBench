#!/usr/bin/env bash
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "====================================="
echo " NeoBench Unified Build System"
echo "====================================="
echo

echo "[1/7] Building libNBFS..."
make -C "$ROOT/libs/libnbfs"

echo
echo "[2/7] Building mkfs.nbfs + root image tools..."
make -C "$ROOT/tools/nbfs/mkfs"
make -C "$ROOT/tools/nbfs/rootimg"

echo
echo "[3/7] Building root NBFS image..."
"$ROOT/scripts/build_rootfs.sh"

echo
echo "[4/7] Building Kernel (embeds root image)..."
make -C "$ROOT/kernel"

echo
echo "[5/7] Building Loader + FS-UAE binary..."
"$ROOT/scripts/build-fsuae.sh"

echo
echo "[6/7] Creating HDF image..."
mkdir -p "$ROOT/images"
python3 "$ROOT/scripts/build_hdf.py" \
    "$ROOT/rootfs/boot/kernel.elf" \
    "$ROOT/images/neobench.hdf" \
    16

echo
echo "[7/7] Build Complete"

echo
echo "Artifacts:"
echo "  Kernel : rootfs/boot/kernel.elf"
echo "  Loader : rootfs/boot/neoloader-fsuae.bin"
echo "  Root FS: rootfs/boot/neobench-root.nbfs"
echo "  HDF    : images/neobench.hdf"