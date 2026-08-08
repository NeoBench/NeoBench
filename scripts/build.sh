#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "====================================="
echo " NeoBench Unified Build System"
echo "====================================="
echo

echo "[1/7] Building libNBFS..."
make -C "$ROOT/libs/libnbfs"

echo
echo "[2/7] Building mkfs.nbfs..."
make -C "$ROOT/tools/nbfs/mkfs"

echo
echo "[3/7] Building Kernel..."
make -C "$ROOT/kernel"

echo
echo "[4/7] Building Loader..."
make -C "$ROOT/loader"

echo
echo "[5/7] Preparing RootFS..."
mkdir -p "$ROOT/rootfs/boot"
cp "$ROOT/kernel/kernel.elf" "$ROOT/rootfs/boot/kernel.elf"

echo
echo "[6/7] Creating NBFS Image..."
mkdir -p "$ROOT/images"

"$ROOT/tools/nbfs/mkfs/build/bin/mkfs.nbfs" \
"$ROOT/images/neobench.img"

echo
echo "[7/7] Build Complete"

echo
echo "Artifacts:"
echo "  Kernel : kernel/kernel.elf"
echo "  Loader : loader/loader.elf"
echo "  Image  : images/neobench.img"
