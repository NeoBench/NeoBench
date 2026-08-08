#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BSD="$ROOT/bsd"
FREEBSD="$BSD/freebsd-src"
BRANCH=${FREEBSD_BRANCH:-stable/15}

if [ ! -d "$FREEBSD/.git" ]; then
    git clone --branch "$BRANCH" --single-branch \
        https://github.com/freebsd/freebsd-src.git "$FREEBSD"
else
    git -C "$FREEBSD" fetch origin "$BRANCH"
    git -C "$FREEBSD" checkout "$BRANCH"
    git -C "$FREEBSD" reset --hard "origin/$BRANCH"
fi

printf '%s\n' 'NeoBench: applying FreeBSD build integration'
git -C "$FREEBSD" apply "$BSD/patches/0001-enable-neobench-m68k-target.patch"

# Keep the NeoBench MD sources in the project tree.  The FreeBSD source tree
# receives a generated copy for the current build; it is never committed back
# into the upstream checkout.
mkdir -p "$FREEBSD/sys/m68k/neobench"
cp "$ROOT/kernel/arch/m68k/neobench/boot.S" "$FREEBSD/sys/m68k/neobench/boot.S"
cp "$ROOT/kernel/arch/m68k/neobench/vector_table.S" "$FREEBSD/sys/m68k/neobench/vector_table.S"
cp "$ROOT/kernel/arch/m68k/neobench/early.c" "$FREEBSD/sys/m68k/neobench/early.c"
cp "$ROOT/kernel/arch/m68k/neobench/platform.c" "$FREEBSD/sys/m68k/neobench/platform.c"
cp "$ROOT/kernel/arch/m68k/neobench/mmu.c" "$FREEBSD/sys/m68k/neobench/mmu.c"
cp "$ROOT/kernel/arch/m68k/neobench/exception.c" "$FREEBSD/sys/m68k/neobench/exception.c"
cp "$ROOT/kernel/arch/m68k/neobench/trap.c" "$FREEBSD/sys/m68k/neobench/trap.c"
cp "$ROOT/kernel/arch/m68k/neobench/bootinfo.h" "$FREEBSD/sys/m68k/neobench/bootinfo.h"
cp "$ROOT/kernel/arch/m68k/neobench/trapframe.h" "$FREEBSD/sys/m68k/neobench/trapframe.h"
cp "$ROOT/kernel/arch/m68k/neobench/md.h" "$FREEBSD/sys/m68k/neobench/md.h"

mkdir -p "$FREEBSD/sys/m68k/conf"
cp "$BSD/conf/NEOBENCH" "$FREEBSD/sys/m68k/conf/NEOBENCH"

printf '%s\n' ''
printf '%s\n' 'NeoBench FreeBSD source prepared.'
printf '%s\n' "Source: $FREEBSD"
printf '%s\n' "Config: $FREEBSD/sys/m68k/conf/NEOBENCH"
printf '%s\n' 'NOTE: the MD port is not build-complete yet; FreeBSD m68k VM, trap, and config glue still need implementation.'
