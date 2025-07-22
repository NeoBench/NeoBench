#!/bin/bash
set -e

echo "[NeoBench] 🔄 Cleaning previous build..."
rm -rf build
mkdir -p build

ARCH="m68k-unknown-none"
KERNEL_BIN="build/kernel.bin"
BOOTLOADER_BIN="build/bootloader.bin"
STAGE2_BIN="build/stage2.bin"
FINAL_IMG="build/neoboot.img"

echo "[NeoBench] 🛠 Building neoboot kernel for $ARCH..."
cargo build --release --package neoboot --target $ARCH

echo "[NeoBench] 🔧 Converting kernel ELF to flat binary..."
objcopy -O binary \
  target/$ARCH/release/neoboot \
  $KERNEL_BIN

echo "[NeoBench] ⚙ Assembling bootloader..."
nasm -f bin bootloader/src/main.asm -o $BOOTLOADER_BIN
nasm -f bin bootloader/src/stage2.asm -o $STAGE2_BIN

echo "[NeoBench] 📦 Building NeoInit + NeoShell..."
cargo build --release --package init
cargo build --release --package neosh

echo "[NeoBench] 🧰 Packing initramfs..."
mkdir -p rootfs/bin
cp target/release/init rootfs/bin/init
cp target/release/neosh rootfs/bin/neosh

pushd rootfs >/dev/null
find . | cpio -o --format=newc | gzip > ../build/initramfs.img
popd >/dev/null

echo "[NeoBench] 🧬 Linking image components..."
cat $BOOTLOADER_BIN $STAGE2_BIN $KERNEL_BIN > $FINAL_IMG

echo "[NeoBench] ✅ NeoBench boot image built: $FINAL_IMG"
echo "[NeoBench] 📦 Initramfs: build/initramfs.img"

# Optional QEMU boot
if [[ "$1" == "--run" ]]; then
  echo "[NeoBench] 🚀 Booting in QEMU..."
  qemu-system-m68k \
    -M q800 \
    -kernel $FINAL_IMG \
    -initrd build/initramfs.img \
    -nographic \
    -serial mon:stdio \
    -append "console=ttyS0"
fi
