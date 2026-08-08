#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench System Tests"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p tests
mkdir -p tests/boot
mkdir -p tests/kernel
mkdir -p tests/fs
mkdir -p tests/drivers
mkdir -p tests/memory
mkdir -p tests/scheduler
mkdir -p tests/syscalls
mkdir -p tests/userspace
mkdir -p tests/network
mkdir -p tests/gui
mkdir -p tests/emulator
mkdir -p tests/performance
mkdir -p tests/stress
mkdir -p tests/regression
mkdir -p tests/docs

###############################################################################
# Boot Tests
###############################################################################

touch tests/boot/test_boot.c
touch tests/boot/test_bootloader.c
touch tests/boot/test_kernel_entry.c

###############################################################################
# Filesystem Tests
###############################################################################

touch tests/fs/test_nbfs.c
touch tests/fs/test_superblock.c
touch tests/fs/test_inode.c
touch tests/fs/test_directory.c
touch tests/fs/test_bitmap.c

###############################################################################
# Memory Tests
###############################################################################

touch tests/memory/test_pmm.c
touch tests/memory/test_vmm.c
touch tests/memory/test_heap.c

###############################################################################
# Scheduler Tests
###############################################################################

touch tests/scheduler/test_scheduler.c
touch tests/scheduler/test_threads.c

###############################################################################
# Driver Tests
###############################################################################

touch tests/drivers/test_pci.c
touch tests/drivers/test_usb.c
touch tests/drivers/test_storage.c

###############################################################################
# Userspace
###############################################################################

touch tests/userspace/test_shell.c
touch tests/userspace/test_init.c

###############################################################################
# Emulator Scripts
###############################################################################

touch tests/emulator/run_qemu.sh
touch tests/emulator/run_fsuae.sh
touch tests/emulator/run_amiberry.sh

chmod +x tests/emulator/*.sh

###############################################################################
# Documentation
###############################################################################

touch tests/docs/README.md
touch tests/docs/TEST_PLAN.md
touch tests/docs/REGRESSION.md

###############################################################################
# Top-level Makefile
###############################################################################

cat > tests/Makefile <<'EOF'
all:
	@echo "Running NeoBench test suite..."

boot:
	@echo "Running boot tests..."

kernel:
	@echo "Running kernel tests..."

fs:
	@echo "Running filesystem tests..."

clean:
	@echo "Cleaning test outputs..."

.PHONY: all boot kernel fs clean
EOF

echo
echo "========================================"
echo " System Test Framework Created"
echo "========================================"

find tests | sort
