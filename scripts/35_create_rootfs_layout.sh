#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

ROOTFS="rootfs"

echo "========================================"
echo " Creating NeoBench Root Filesystem"
echo "========================================"

###############################################################################
# Standard directories
###############################################################################

DIRS=(
boot
bin
sbin
lib
lib/modules
lib/firmware
etc
etc/dinit.d
etc/network
etc/fonts
etc/themes
usr
usr/bin
usr/sbin
usr/lib
usr/include
usr/share
usr/share/fonts
usr/share/icons
usr/share/themes
var
var/log
var/cache
var/tmp
tmp
home
home/root
home/demo
opt
srv
mnt
media
proc
sys
dev
run
run/dinit
run/lock
)

for d in "${DIRS[@]}"; do
    mkdir -p "$ROOTFS/$d"
done

###############################################################################
# Configuration files
###############################################################################

touch "$ROOTFS/etc/fstab"
touch "$ROOTFS/etc/hostname"
touch "$ROOTFS/etc/passwd"
touch "$ROOTFS/etc/group"
touch "$ROOTFS/etc/profile"
touch "$ROOTFS/etc/motd"
touch "$ROOTFS/etc/neobench.conf"

###############################################################################
# Boot files
###############################################################################

touch "$ROOTFS/boot/kernel.elf"
touch "$ROOTFS/boot/initrd.nbfs"
touch "$ROOTFS/boot/neoloader.conf"

###############################################################################
# Logs
###############################################################################

touch "$ROOTFS/var/log/kernel.log"
touch "$ROOTFS/var/log/dinit.log"

###############################################################################
# Placeholder files
###############################################################################

touch "$ROOTFS/.keep"

echo
echo "Root filesystem layout created."

find "$ROOTFS" | sort
