#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# NeoBench libnbfs Core Generator
###############################################################################

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

LIB="libs/libnbfs"

echo "=========================================="
echo " Creating NeoBench libnbfs Core"
echo "=========================================="

###############################################################################
# Directories
###############################################################################

mkdir -p "$LIB"

mkdir -p "$LIB/include"
mkdir -p "$LIB/include/internal"

mkdir -p "$LIB/src"

mkdir -p "$LIB/tests"

mkdir -p "$LIB/docs"

mkdir -p "$LIB/build"

###############################################################################
# Public Headers
###############################################################################

PUBLIC_HEADERS=(
libnbfs.h
)

for FILE in "${PUBLIC_HEADERS[@]}"
do

cat > "$LIB/include/$FILE" <<EOF
#ifndef LIBNBFS_H
#define LIBNBFS_H

#include <stdint.h>
#include <stdio.h>

#include <nbfs/nbfs.h>

typedef struct nbfs_context nbfs_context_t;

nbfs_context_t *nbfs_create(const char *path);
nbfs_context_t *nbfs_open(const char *path);
void nbfs_close(nbfs_context_t *);

int nbfs_flush(nbfs_context_t *);

int nbfs_read_block(
        nbfs_context_t *,
        uint64_t,
        void *);

int nbfs_write_block(
        nbfs_context_t *,
        uint64_t,
        const void *);

#endif
EOF

done

###############################################################################
# Internal Headers
###############################################################################

INTERNAL_HEADERS=(
context.h
image.h
block.h
endian.h
crc32.h
error.h
)

for FILE in "${INTERNAL_HEADERS[@]}"
do

GUARD=$(echo "$FILE" | tr '[:lower:].' '[:upper:]_' )

cat > "$LIB/include/internal/$FILE" <<EOF
#ifndef ${GUARD}
#define ${GUARD}

#endif
EOF

done

###############################################################################
# Source Files
###############################################################################

SOURCES=(
context.c
image.c
block.c
endian.c
crc32.c
error.c
)

for FILE in "${SOURCES[@]}"
do

cat > "$LIB/src/$FILE" <<EOF
/*
 * $FILE
 * NeoBench libnbfs
 */

#include <stdio.h>
#include <stdlib.h>

#include "internal/context.h"

EOF

done

###############################################################################
# Tests
###############################################################################

TESTS=(
test_image.c
test_block.c
test_crc32.c
)

for FILE in "${TESTS[@]}"
do
touch "$LIB/tests/$FILE"
done

###############################################################################
# Docs
###############################################################################

DOCS=(
README.md
API.md
BLOCK_IO.md
CRC32.md
)

for FILE in "${DOCS[@]}"
do
touch "$LIB/docs/$FILE"
done

###############################################################################
# Makefile
###############################################################################

cat > "$LIB/Makefile" <<'EOF'
CC ?= gcc
AR ?= ar

SRC := $(wildcard src/*.c)

OBJ := $(SRC:.c=.o)

TARGET := build/libnbfs.a

CFLAGS := \
-Wall \
-Wextra \
-std=c11 \
-O2 \
-Iinclude \
-I../../include

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	$(AR) rcs $(TARGET) $(OBJ)

clean:
	rm -rf build
	rm -f src/*.o

.PHONY: all clean
EOF

###############################################################################
# README
###############################################################################

cat > "$LIB/README.md" <<EOF
NeoBench Filesystem Library

Shared by:

- mkfs.nbfs
- fsck.nbfs
- nbfsinfo
- dump.nbfs
- NeoBench Kernel
EOF

###############################################################################
# Summary
###############################################################################

echo
echo "=========================================="
echo " libnbfs Created"
echo "=========================================="
echo

find "$LIB" | sort
