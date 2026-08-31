#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

CC="${M68K_CC:-m68k-elf-gcc}"
LD="${M68K_LD:-m68k-elf-ld}"
OBJCOPY="${M68K_OBJCOPY:-m68k-elf-objcopy}"
NM="${M68K_NM:-m68k-elf-nm}"
READELF="${M68K_READELF:-m68k-elf-readelf}"

BOOT="$ROOT/boot/neoloader"
OUT="$ROOT/rootfs/boot"
TMP="$ROOT/fsuae/.build"

KERNEL="$OUT/kernel.elf"
KERNEL_OBJ="$TMP/kernel-elf.o"
LOADER="$OUT/neoloader.elf"
LOADER_BIN="$OUT/neoloader-fsuae.bin"

CFLAGS="
-m68030
-ffreestanding
-fno-builtin
-fno-stack-protector
-nostdlib
-nostartfiles
-nodefaultlibs
-I$BOOT/include
"

echo "========================================"
echo " NeoBench FS-UAE 68060 build"
echo "========================================"
echo
echo "ROOT      = $ROOT"
echo "CC        = $CC"
echo

mkdir -p "$TMP" "$OUT"

echo "[1/7] Checking kernel..."

test -f "$KERNEL" || {
    echo "ERROR: $KERNEL does not exist."
    exit 1
}

"$READELF" -h "$KERNEL" |
    grep -E 'Class|Data|Machine|Entry|Flags'

echo
echo "[2/7] Embedding kernel ELF..."

rm -f "$KERNEL_OBJ"

"$OBJCOPY" \
    -I binary \
    -O elf32-m68k \
    -B m68k \
    "$KERNEL" \
    "$KERNEL_OBJ"

#
# objcopy creates symbols from the complete input pathname.
# Normalize them to the names expected by NeoLoader.
#
ORIG_PREFIX="_binary_$(printf '%s' "$KERNEL" | sed 's|/|_|g')"

"$OBJCOPY" \
    --redefine-sym "${ORIG_PREFIX}_start=_binary_kernel_elf_start" \
    --redefine-sym "${ORIG_PREFIX}_end=_binary_kernel_elf_end" \
    --redefine-sym "${ORIG_PREFIX}_size=_binary_kernel_elf_size" \
    "$KERNEL_OBJ"

echo
echo "  Embedded symbols:"
"$NM" "$KERNEL_OBJ"

echo
echo "[3/7] Building NeoLoader startup..."

"$CC" $CFLAGS \
    -c "$BOOT/arch/m68k/start.S" \
    -o "$TMP/neoloader-start.o"

echo
echo "[4/7] Building NeoLoader main..."

"$CC" $CFLAGS \
    -c "$BOOT/src/main.c" \
    -o "$TMP/neoloader-main.o"

echo
echo "[5/7] Building NeoLoader ELF loader..."

"$CC" $CFLAGS \
    -c "$BOOT/src/elf_loader.c" \
    -o "$TMP/elf-loader.o"

echo
echo "[6/7] Building NeoLoader console..."

"$CC" $CFLAGS \
    -c "$BOOT/src/console.c" \
    -o "$TMP/console.o"

echo
echo "[6b] Building NeoLoader 3D boot art..."

"$CC" $CFLAGS \
    -c "$BOOT/src/bootart.c" \
    -o "$TMP/bootart.o"

echo
echo "[7/7] Building NeoLoader loader/transfer..."

"$CC" $CFLAGS \
    -c "$BOOT/src/loader.c" \
    -o "$TMP/loader.o"

"$CC" $CFLAGS \
    -c "$BOOT/arch/m68k/transfer.S" \
    -o "$TMP/transfer.o"

echo
echo "  Objects:"
ls -lh \
    "$KERNEL_OBJ" \
    "$TMP/neoloader-start.o" \
    "$TMP/neoloader-main.o" \
    "$TMP/console.o" \
    "$TMP/bootart.o" \
    "$TMP/elf-loader.o" \
    "$TMP/loader.o" \
    "$TMP/transfer.o"

echo
echo "[8/8] Linking NeoLoader..."

rm -f "$LOADER"

"$LD" \
    -T "$BOOT/linker/neoloader.ld" \
    -o "$LOADER" \
    "$TMP/neoloader-start.o" \
    "$TMP/neoloader-main.o" \
    "$TMP/console.o" \
    "$TMP/bootart.o" \
    "$TMP/elf-loader.o" \
    "$TMP/loader.o" \
    "$TMP/transfer.o" \
    "$KERNEL_OBJ"

echo
echo "========================================"
echo " NeoLoader ELF"
echo "========================================"

"$READELF" -h "$LOADER" |
    grep -E 'Class|Data|Machine|Entry|Flags'

echo
echo "========================================"
echo " NeoLoader segments"
echo "========================================"

"$READELF" -l "$LOADER"

echo
echo "========================================"
echo " Kernel embedded symbols"
echo "========================================"

"$NM" "$LOADER" |
    grep -E \
    '(_binary_kernel_elf_start|_binary_kernel_elf_end|_binary_kernel_elf_size|neo_main|neo_load_kernel|neo_elf_load_memory|neo_jump_to_kernel|_start)'

echo
echo "========================================"
echo " Creating FS-UAE binary"
echo "========================================"

"$OBJCOPY" \
    -O binary \
    "$LOADER" \
    "$LOADER_BIN"

echo
echo "========================================"
echo " BUILD COMPLETE"
echo "========================================"

stat -c '%n %s bytes' \
    "$KERNEL" \
    "$LOADER" \
    "$LOADER_BIN"

echo
echo "FS-UAE loader:"
echo "  $LOADER_BIN"

echo
echo "NeoLoader ELF:"
echo "  $LOADER"

echo
echo "Kernel:"
echo "  $KERNEL"
