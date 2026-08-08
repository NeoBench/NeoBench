#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench HAL"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p kernel/hal
mkdir -p kernel/hal/include
mkdir -p kernel/hal/platform
mkdir -p kernel/hal/m68k
mkdir -p kernel/hal/devices
mkdir -p kernel/hal/tests
mkdir -p kernel/hal/docs

###############################################################################
# Core HAL Sources
###############################################################################

CORE=(
hal.c
init.c
cpu.c
memory.c
interrupt.c
timer.c
cache.c
dma.c
)

for f in "${CORE[@]}"
do
cat > kernel/hal/$f <<EOF
/*
 * $f
 * NeoBench Hardware Abstraction Layer
 */
EOF
done

###############################################################################
# Headers
###############################################################################

HEADERS=(
hal.h
init.h
cpu.h
memory.h
interrupt.h
timer.h
cache.h
dma.h
)

for f in "${HEADERS[@]}"
do
GUARD=$(echo "$f" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')

cat > kernel/hal/include/$f <<EOF
#ifndef $GUARD
#define $GUARD

#endif
EOF
done

###############################################################################
# m68k Platform
###############################################################################

PLATFORM=(
startup.c
exceptions.c
vectors.c
mmu.c
fpu.c
cpu030.c
cpu040.c
cpu060.c
)

for f in "${PLATFORM[@]}"
do
touch kernel/hal/m68k/$f
done

###############################################################################
# Devices
###############################################################################

DEVICES=(
console.c
rtc.c
serial.c
parallel.c
keyboard.c
mouse.c
)

for f in "${DEVICES[@]}"
do
touch kernel/hal/devices/$f
done

###############################################################################
# Tests
###############################################################################

TESTS=(
test_cpu.c
test_timer.c
test_memory.c
)

for f in "${TESTS[@]}"
do
touch kernel/hal/tests/$f
done

###############################################################################
# Docs
###############################################################################

DOCS=(
README.md
CPU.md
MEMORY.md
INTERRUPTS.md
)

for f in "${DOCS[@]}"
do
touch kernel/hal/docs/$f
done

###############################################################################
# Makefile
###############################################################################

cat > kernel/hal/Makefile <<'EOF'
CC ?= m68k-elf-gcc

SRC := $(wildcard *.c)
OBJ := $(SRC:.c=.o)

CFLAGS := \
-Wall \
-Wextra \
-std=c11 \
-O2 \
-Iinclude

all: $(OBJ)

clean:
	rm -f *.o

.PHONY: all clean
EOF

echo
echo "HAL created."

find kernel/hal | sort
