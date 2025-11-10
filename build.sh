#!/usr/bin/env bash
set -e

CPU=68000
TARGET_JSON="neobench-m68k-${CPU}.json"
OUT_ROM="roms/neobench-${CPU}.rom"

mkdir -p roms

echo "[NeoBench] Building minimal ROM for $CPU…"

cargo build --release --target $TARGET_JSON

ELF="target/${TARGET_JSON%.*}/release/neobench"

if [ ! -f "$ELF" ]; then
    echo "[NeoBench] ERROR: ELF not found at $ELF"
    exit 1
fi

m68k-amigaos-objcopy -O binary "$ELF" "$OUT_ROM"

ROM_SIZE=$(stat -c%s "$OUT_ROM" 2>/dev/null || stat -f%z "$OUT_ROM")
echo "[NeoBench] ROM built → $OUT_ROM ($ROM_SIZE bytes)"
