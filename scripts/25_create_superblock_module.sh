#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo "Creating NBFS Superblock Module"
echo "========================================"

###############################################################################
# libnbfs
###############################################################################

mkdir -p libs/libnbfs/include/internal
mkdir -p libs/libnbfs/src
mkdir -p libs/libnbfs/tests

touch libs/libnbfs/include/internal/superblock.h
touch libs/libnbfs/include/internal/crc.h

touch libs/libnbfs/src/superblock.c
touch libs/libnbfs/src/crc.c

touch libs/libnbfs/tests/test_superblock.c

###############################################################################
# Kernel
###############################################################################

mkdir -p kernel/fs/nbfs

touch kernel/fs/nbfs/superblock.c
touch kernel/fs/nbfs/superblock.h
touch kernel/fs/nbfs/mount.c
touch kernel/fs/nbfs/mount.h

###############################################################################
# Documentation
###############################################################################

mkdir -p docs/nbfs

touch docs/nbfs/SUPERBLOCK.md
touch docs/nbfs/MOUNT.md
touch docs/nbfs/CRC.md

###############################################################################
# nbfsinfo
###############################################################################

mkdir -p tools/nbfsinfo/src
mkdir -p tools/nbfsinfo/include
mkdir -p tools/nbfsinfo/tests

touch tools/nbfsinfo/src/main.c
touch tools/nbfsinfo/Makefile
touch tools/nbfsinfo/README.md

echo
echo "NBFS Superblock module created."

find libs/libnbfs kernel/fs/nbfs tools/nbfsinfo docs/nbfs | sort
