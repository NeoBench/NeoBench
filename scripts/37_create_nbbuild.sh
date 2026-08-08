#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NBBuild"
echo "========================================"

mkdir -p tools/nbbuild/{src,include,config,docs,tests,build}

##############################################################################
# Source
##############################################################################

FILES=(
main.c
config.c
clean.c
compile.c
kernel.c
bootloader.c
libs.c
sdk.c
userspace.c
tools.c
image.c
package.c
release.c
run.c
util.c
logging.c
)

for f in "${FILES[@]}"
do
cat > tools/nbbuild/src/$f <<EOF
/*
 * $f
 * NeoBench Build System
 */
EOF
done

##############################################################################
# Headers
##############################################################################

HEADERS=(
config.h
compile.h
kernel.h
bootloader.h
image.h
package.h
release.h
run.h
util.h
logging.h
)

for h in "${HEADERS[@]}"
do
GUARD=$(echo "$h" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')

cat > tools/nbbuild/include/$h <<EOF
#ifndef $GUARD
#define $GUARD

#endif
EOF
done

##############################################################################
# Config
##############################################################################

cat > tools/nbbuild/config/default.conf <<EOF
ARCH=m68k
CPU=68060
BUILD=Release
BOOTLOADER=NeoLoader
FILESYSTEM=NBFS
INIT=Dinit
DESKTOP=NeoDesktop
EOF

##############################################################################
# Tests
##############################################################################

touch tools/nbbuild/tests/test_config.c
touch tools/nbbuild/tests/test_build.c

##############################################################################
# Docs
##############################################################################

touch tools/nbbuild/docs/README.md
touch tools/nbbuild/docs/CONFIG.md
touch tools/nbbuild/docs/COMMANDS.md

##############################################################################
# Makefile
##############################################################################

cat > tools/nbbuild/Makefile <<'EOF'
CC ?= gcc

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

CFLAGS := -Wall -Wextra -std=c11 -O2 -Iinclude

all: nbbuild

nbbuild: $(OBJ)
	$(CC) $(OBJ) -o nbbuild

clean:
	rm -f src/*.o nbbuild

.PHONY: all clean
EOF

echo
echo "NBBuild created."

find tools/nbbuild | sort
