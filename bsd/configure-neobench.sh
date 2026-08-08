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

printf '%s\n' 'NeoBench: enabling m68k build target'

# Do not depend on line numbers or a brittle unified patch here.  FreeBSD's
# stable/15 Makefile.inc1 can change around the architecture list.  Insert the
# NeoBench target immediately after riscv64/riscv when it is not already there.
MAKEFILE="$FREEBSD/Makefile.inc1"
if ! grep -Eq '^[[:space:]]*m68k/m68k([[:space:]]|\\|$)' "$MAKEFILE"; then
    awk '
        /riscv64\/riscv/ && !done {
            printf "%s %c\n", $0, 92;
            print "\t\tm68k/m68k";
            done=1;
            next;
        }
        { print }
    ' "$MAKEFILE" > "$MAKEFILE.neobench"
    mv "$MAKEFILE.neobench" "$MAKEFILE"
fi

# Keep the NeoBench MD sources in the project tree. The FreeBSD source tree
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
