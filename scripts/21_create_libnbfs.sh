#!/usr/bin/env bash
set -Eeuo pipefail

###############################################################################
# Find NeoBench root
###############################################################################

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

cd "$ROOT"

echo
echo "========================================"
echo " Creating libnbfs"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p libs/libnbfs
mkdir -p libs/libnbfs/include
mkdir -p libs/libnbfs/src
mkdir -p libs/libnbfs/tests
mkdir -p libs/libnbfs/docs
mkdir -p libs/libnbfs/build

###############################################################################
# Headers
###############################################################################

HEADERS=(
libnbfs.h
image.h
superblock.h
inode.h
directory.h
bitmap.h
journal.h
crc32.h
)

for H in "${HEADERS[@]}"
do
    touch "libs/libnbfs/include/$H"
done

###############################################################################
# Sources
###############################################################################

SOURCES=(
image.c
superblock.c
inode.c
directory.c
bitmap.c
journal.c
crc32.c
libnbfs.c
)

for S in "${SOURCES[@]}"
do
cat > "libs/libnbfs/src/$S" <<EOF
/*
 * $S
 * libnbfs
 */

EOF
done

###############################################################################
# Makefile
###############################################################################

cat > libs/libnbfs/Makefile <<'EOF'
CC=gcc

CFLAGS=-Wall -Wextra -O2 -Iinclude

SRC=$(wildcard src/*.c)

OBJ=$(SRC:.c=.o)

TARGET=build/libnbfs.a

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	ar rcs $(TARGET) $(OBJ)

clean:
	rm -rf build src/*.o
EOF

###############################################################################

echo
echo "libnbfs created."
