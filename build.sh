#!/bin/bash
set -e

echo "[NeoBench] Cleaning..."
cargo clean --manifest-path neoboot/Cargo.toml
rm -rf build
mkdir -p build

echo "[NeoBench] Building Rust kernel..."
cargo build --release --manifest-path neoboot/Cargo.toml --target x86_64-unknown-none

echo "[NeoBench] Converting kernel to flat binary..."
objcopy -O binary neoboot/target/x86_64-unknown-none/release/neoboot build/kernel.bin

echo "[NeoBench] Building bootloader..."
cd bootloader
make clean
make
cd ..

echo "[NeoBench] Build complete. Output image: build/neoboot.img"
