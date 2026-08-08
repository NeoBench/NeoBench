#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench VFS Layout"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p kernel/fs/vfs
mkdir -p kernel/fs/vfs/include
mkdir -p kernel/fs/vfs/tests
mkdir -p kernel/fs/vfs/docs

###############################################################################
# Source files
###############################################################################

SOURCES=(
vfs.c
mount.c
vnode.c
dentry.c
path.c
file.c
filesystem.c
)

for f in "${SOURCES[@]}"; do
cat > "kernel/fs/vfs/$f" <<EOF
/*
 * $f
 * NeoBench Virtual Filesystem
 */
EOF
done

###############################################################################
# Header files
###############################################################################

HEADERS=(
vfs.h
mount.h
vnode.h
dentry.h
path.h
file.h
filesystem.h
)

for f in "${HEADERS[@]}"; do
cat > "kernel/fs/vfs/include/$f" <<EOF
#ifndef VFS_$(echo "$f" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')
#define VFS_$(echo "$f" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')

#endif
EOF
done

###############################################################################
# Tests
###############################################################################

TESTS=(
test_vfs.c
test_path.c
test_mount.c
)

for f in "${TESTS[@]}"; do
touch "kernel/fs/vfs/tests/$f"
done

###############################################################################
# Documentation
###############################################################################

DOCS=(
README.md
ARCHITECTURE.md
API.md
)

for f in "${DOCS[@]}"; do
touch "kernel/fs/vfs/docs/$f"
done

###############################################################################
# Makefile
###############################################################################

cat > kernel/fs/vfs/Makefile <<'EOF'
CC ?= gcc

SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)

CFLAGS := -Wall -Wextra -std=c11 -O2 -Iinclude

all: $(OBJ)

clean:
	rm -f *.o

.PHONY: all clean
EOF

echo
echo "========================================"
echo " VFS Layout Created"
echo "========================================"

find kernel/fs/vfs | sort
