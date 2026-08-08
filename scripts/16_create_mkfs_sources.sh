#!/usr/bin/env bash
#
# NeoBench mkfs.nbfs Project Generator
#

set -Eeuo pipefail

###############################################################################
# Find NeoBench project root
###############################################################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="$SCRIPT_DIR"

while [[ "$PROJECT_ROOT" != "/" ]]; do
    if [[ -f "$PROJECT_ROOT/README.md" || -f "$PROJECT_ROOT/Makefile" ]]; then
        break
    fi
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done

if [[ "$PROJECT_ROOT" == "/" ]]; then
    echo "Error: Could not locate NeoBench project root."
    exit 1
fi

cd "$PROJECT_ROOT"

ROOT="tools/nbfs/mkfs"

echo
echo "========================================"
echo " NeoBench mkfs.nbfs Project Generator"
echo "========================================"
echo
echo "Project Root : $PROJECT_ROOT"
echo "Destination  : $ROOT"
echo

###############################################################################
# Directories
###############################################################################

DIRECTORIES=(
"$ROOT"
"$ROOT/src"
"$ROOT/include"
"$ROOT/docs"
"$ROOT/tests"
"$ROOT/build"
"$ROOT/build/bin"
"$ROOT/build/obj"
)

for DIR in "${DIRECTORIES[@]}"; do
    mkdir -p "$DIR"
done

###############################################################################
# Source Files
###############################################################################

SOURCE_FILES=(
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

for FILE in "${SOURCE_FILES[@]}"; do

    TARGET="$ROOT/src/$FILE"

    if [[ ! -f "$TARGET" ]]; then

cat > "$TARGET" <<EOF
/*
 * $FILE
 * NeoBench mkfs.nbfs
 */

#include <stdio.h>

EOF

    fi

done

###############################################################################
# Header Files
###############################################################################

HEADER_FILES=(
mkfs.h
nbfs.h
image.h
layout.h
superblock.h
bitmap.h
inode.h
extent.h
directory.h
rootdir.h
journal.h
crc32.h
verify.h
util.h
)

for FILE in "${HEADER_FILES[@]}"; do

    TARGET="$ROOT/include/$FILE"

    if [[ ! -f "$TARGET" ]]; then

        GUARD=$(echo "$FILE" | tr '[:lower:].' '[:upper:]_')

cat > "$TARGET" <<EOF
#ifndef ${GUARD}
#define ${GUARD}

/*
 * $FILE
 */

#endif
EOF

    fi

done

###############################################################################
# Documentation
###############################################################################

DOC_FILES=(
README.md
DESIGN.md
LAYOUT.md
SUPERBLOCK.md
INODES.md
DIRECTORIES.md
JOURNAL.md
FORMAT.md
API.md
)

for FILE in "${DOC_FILES[@]}"; do

    TARGET="$ROOT/docs/$FILE"

    if [[ ! -f "$TARGET" ]]; then

cat > "$TARGET" <<EOF
# ${FILE%.md}

NeoBench mkfs.nbfs documentation.
EOF

    fi

done

###############################################################################
# Tests
###############################################################################

TEST_FILES=(
test_image.c
test_superblock.c
test_bitmap.c
test_inode.c
test_directory.c
test_crc32.c
)

for FILE in "${TEST_FILES[@]}"; do

    touch "$ROOT/tests/$FILE"

done

###############################################################################
# Makefile
###############################################################################

if [[ ! -f "$ROOT/Makefile" ]]; then

cat > "$ROOT/Makefile" <<'EOF'
CC ?= gcc

CFLAGS = -Wall -Wextra -O2 -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC))

TARGET := build/bin/mkfs.nbfs

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

fi

###############################################################################
# Git Ignore
###############################################################################

if [[ ! -f "$ROOT/.gitignore" ]]; then

cat > "$ROOT/.gitignore" <<EOF
build/
*.img
*.iso
*.nbfs
EOF

fi

###############################################################################
# Summary
###############################################################################

echo
echo "========================================"
echo " mkfs.nbfs Project Ready"
echo "========================================"
echo

echo "Directories:"
find "$ROOT" -type d | sort

echo
echo "Files:"
find "$ROOT" -type f | sort

echo
echo "Done."
