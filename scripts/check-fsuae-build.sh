#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT="$ROOT/rootfs/boot"

echo "===== NEOLOADER ARTIFACTS ====="

test -f "$OUT/kernel.elf"
test -f "$OUT/neoloader.elf"
test -f "$OUT/neoloader.bin"

stat -c '%n %s bytes' \
    "$OUT/kernel.elf" \
    "$OUT/neoloader.elf" \
    "$OUT/neoloader.bin"

echo
echo "===== KERNEL ====="

m68k-elf-readelf -h "$OUT/kernel.elf" |
    grep -E 'Class|Data|Machine|Entry|Flags'

echo
echo "===== NEOLOADER ====="

m68k-elf-readelf -h "$OUT/neoloader.elf" |
    grep -E 'Class|Data|Machine|Entry|Flags'

echo
echo "===== UNDEFINED ====="

if m68k-elf-nm "$OUT/neoloader.elf" | grep ' U '; then
    echo "ERROR: unresolved symbols"
    exit 1
fi

echo "NONE"

echo
echo "===== EMBEDDED KERNEL ====="

m68k-elf-nm "$OUT/neoloader.elf" |
    grep '_binary_rootfs_boot_kernel_elf'

echo
echo "===== KERNEL ENTRY ====="

m68k-elf-nm "$OUT/kernel.elf" |
    grep ' T _start'

echo
echo "===== LOADER ENTRY ====="

m68k-elf-nm "$OUT/neoloader.elf" |
    grep ' T _start'

echo
echo "===== FS-UAE CONFIG ====="

test -f "$ROOT/fsuae/NeoBench-060.fs-uae"
cat "$ROOT/fsuae/NeoBench-060.fs-uae"

echo
echo "FS-UAE development artifacts: PASS"
