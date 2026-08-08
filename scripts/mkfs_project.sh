#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

ROOT="tools/nbfs/mkfs"

echo "======================================="
echo " Creating mkfs.nbfs Project"
echo "======================================="

mkdir -p "$ROOT"/{src,include,build,obj,bin,docs,tests}

###############################################################################
# Source Files
###############################################################################

SOURCES=(
main.c
mkfs.c
image.c
layout.c
superblock.c
bitmap.c
inode.c
extent.c
directory.c
rootdir.c
journal.c
crc32.c
verify.c
util.c
)

for f in "${SOURCES[@]}"; do
cat > "$ROOT/src/$f" <<EOF
/*
 * $f
 * NeoBench mkfs.nbfs
 */

#include <stdio.h>

EOF
done

###############################################################################
# Header Files
###############################################################################

HEADERS=(
mkfs.h
nbfs.h
image.h
layout.h
superblock.h
bitmap.h
inode.h
extent.h
directory.h
journal.h
crc32.h
verify.h
util.h
)

for f in "${HEADERS[@]}"; do
cat > "$ROOT/include/$f" <<EOF
#ifndef ${f^^}
#define ${f^^}

/* $f */

#endif
EOF

done

###############################################################################
# README
###############################################################################

cat > "$ROOT/README.md" <<EOF
# mkfs.nbfs

NeoBench filesystem formatter.

Responsibilities

- Create NBFS images
- Initialise superblocks
- Create bitmaps
- Create inode tables
- Create root directory
- Verify filesystem
EOF

###############################################################################
# Makefile
###############################################################################

cat > "$ROOT/Makefile" <<'EOF'
CC ?= gcc

CFLAGS = -Wall -Wextra -O2 -Iinclude

SRC = $(wildcard src/*.c)

OBJ = $(SRC:src/%.c=build/obj/%.o)

TARGET = build/bin/mkfs.nbfs

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build/bin
	$(CC) $(OBJ) -o $(TARGET)

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

run: all
	./$(TARGET)

.PHONY: all clean run
EOF

###############################################################################
# Documentation
###############################################################################

touch "$ROOT/docs/design.md"
touch "$ROOT/docs/layout.md"
touch "$ROOT/docs/api.md"

###############################################################################
# Tests
###############################################################################

touch "$ROOT/tests/test_superblock.c"
touch "$ROOT/tests/test_bitmap.c"
touch "$ROOT/tests/test_inode.c"

echo
echo "======================================="
echo " mkfs.nbfs Project Created"
echo "======================================="
echo
echo "Location:"
echo "  $ROOT"
echo
echo "Tree:"
tree "$ROOT" -L 2
