#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoLoader"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p boot/neoloader
mkdir -p boot/neoloader/include
mkdir -p boot/neoloader/arch/m68k
mkdir -p boot/neoloader/fs
mkdir -p boot/neoloader/video
mkdir -p boot/neoloader/config
mkdir -p boot/neoloader/modules
mkdir -p boot/neoloader/tests
mkdir -p boot/neoloader/docs
mkdir -p boot/neoloader/build

###############################################################################
# Core
###############################################################################

CORE=(
main.c
boot.c
loader.c
elf.c
memory.c
console.c
panic.c
)

for f in "${CORE[@]}"; do
cat > "boot/neoloader/$f" <<EOF
/*
 * $f
 * NeoLoader
 */
EOF
done

###############################################################################
# Headers
###############################################################################

HEADERS=(
boot.h
loader.h
elf.h
memory.h
console.h
panic.h
config.h
)

for f in "${HEADERS[@]}"; do
GUARD=$(echo "$f" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')

cat > "boot/neoloader/include/$f" <<EOF
#ifndef ${GUARD}
#define ${GUARD}

#endif
EOF
done

###############################################################################
# Architecture
###############################################################################

touch boot/neoloader/arch/m68k/start.S
touch boot/neoloader/arch/m68k/cpu.c
touch boot/neoloader/arch/m68k/mmu.c

###############################################################################
# Filesystem
###############################################################################

touch boot/neoloader/fs/nbfs.c
touch boot/neoloader/fs/vfs.c

###############################################################################
# Video
###############################################################################

touch boot/neoloader/video/framebuffer.c

###############################################################################
# Configuration
###############################################################################

touch boot/neoloader/config/neoloader.conf

###############################################################################
# Modules
###############################################################################

touch boot/neoloader/modules/module.c

###############################################################################
# Documentation
###############################################################################

touch boot/neoloader/docs/README.md
touch boot/neoloader/docs/BOOT_FLOW.md
touch boot/neoloader/docs/ELF.md

###############################################################################
# Tests
###############################################################################

touch boot/neoloader/tests/test_loader.c

###############################################################################
# Makefile
###############################################################################

cat > boot/neoloader/Makefile <<'EOF'
CC ?= m68k-elf-gcc

CFLAGS := -Wall -Wextra -std=c11 -O2 -Iinclude

SRC := $(shell find . -name '*.c')
OBJ := $(SRC:.c=.o)

all: $(OBJ)

clean:
	find . -name '*.o' -delete

.PHONY: all clean
EOF

echo
echo "NeoLoader created."

find boot/neoloader | sort
