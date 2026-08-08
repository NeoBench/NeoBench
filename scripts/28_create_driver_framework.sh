#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench Driver Framework"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p kernel/drivers
mkdir -p kernel/drivers/include
mkdir -p kernel/drivers/core
mkdir -p kernel/drivers/storage
mkdir -p kernel/drivers/network
mkdir -p kernel/drivers/video
mkdir -p kernel/drivers/audio
mkdir -p kernel/drivers/input
mkdir -p kernel/drivers/usb
mkdir -p kernel/drivers/pci
mkdir -p kernel/drivers/zorro
mkdir -p kernel/drivers/tests
mkdir -p kernel/drivers/docs

###############################################################################
# Core Driver Manager
###############################################################################

CORE=(
driver.c
manager.c
bus.c
device.c
probe.c
registry.c
)

for f in "${CORE[@]}"
do
cat > "kernel/drivers/core/$f" <<EOF
/*
 * $f
 * NeoBench Driver Framework
 */
EOF
done

###############################################################################
# Public Headers
###############################################################################

HEADERS=(
driver.h
manager.h
bus.h
device.h
registry.h
)

for f in "${HEADERS[@]}"
do
GUARD=$(echo "$f" | tr '[:lower:].' '[:upper:]_' | tr '.' '_')

cat > "kernel/drivers/include/$f" <<EOF
#ifndef $GUARD
#define $GUARD

#endif
EOF
done

###############################################################################
# Storage Drivers
###############################################################################

mkdir -p kernel/drivers/storage/{ata,sata,scsi,nvme,ramdisk}

touch kernel/drivers/storage/ata/ata.c
touch kernel/drivers/storage/sata/sata.c
touch kernel/drivers/storage/scsi/scsi.c
touch kernel/drivers/storage/nvme/nvme.c
touch kernel/drivers/storage/ramdisk/ramdisk.c

###############################################################################
# Network Drivers
###############################################################################

mkdir -p kernel/drivers/network/{rtl8139,ne2000}

touch kernel/drivers/network/rtl8139/rtl8139.c
touch kernel/drivers/network/ne2000/ne2000.c

###############################################################################
# Video Drivers
###############################################################################

mkdir -p kernel/drivers/video/{radeon,vesa}

touch kernel/drivers/video/radeon/radeon.c
touch kernel/drivers/video/vesa/vesa.c

###############################################################################
# Audio Drivers
###############################################################################

mkdir -p kernel/drivers/audio/{ahi,sb128}

touch kernel/drivers/audio/ahi/ahi.c
touch kernel/drivers/audio/sb128/sb128.c

###############################################################################
# USB
###############################################################################

mkdir -p kernel/drivers/usb/{core,ehci,ohci,uhci}

touch kernel/drivers/usb/core/usb.c
touch kernel/drivers/usb/ehci/ehci.c
touch kernel/drivers/usb/ohci/ohci.c
touch kernel/drivers/usb/uhci/uhci.c

###############################################################################
# PCI & Zorro
###############################################################################

touch kernel/drivers/pci/pci.c
touch kernel/drivers/zorro/zorro.c

###############################################################################
# Documentation
###############################################################################

touch kernel/drivers/docs/README.md
touch kernel/drivers/docs/DRIVER_API.md
touch kernel/drivers/docs/PCI.md
touch kernel/drivers/docs/ZORRO.md

###############################################################################
# Tests
###############################################################################

touch kernel/drivers/tests/test_registry.c
touch kernel/drivers/tests/test_probe.c

###############################################################################
# Makefile
###############################################################################

cat > kernel/drivers/Makefile <<'EOF'
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
echo "Driver framework created."

find kernel/drivers | sort
