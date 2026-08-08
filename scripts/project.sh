#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

echo "Creating $PROJECT project..."

mkdir -p "$PROJECT"

cd "$PROJECT"

###############################################################################
# Top Level
###############################################################################

mkdir -p \
boot \
kernel \
hal \
modules \
libraries \
userland \
etc \
sdk \
tools \
tests \
docs \
build \
out \
scripts \
assets

###############################################################################
# Boot
###############################################################################

mkdir -p \
boot/loader \
boot/rom \
boot/include

###############################################################################
# Kernel
###############################################################################

mkdir -p \
kernel/arch/m68k \
kernel/cpu \
kernel/mm \
kernel/scheduler \
kernel/ipc \
kernel/syscall \
kernel/fs \
kernel/device \
kernel/drivers \
kernel/security \
kernel/init \
kernel/debug

###############################################################################
# HAL
###############################################################################

mkdir -p \
hal/amiga \
hal/pci \
hal/zorro \
hal/mediator \
hal/video \
hal/audio \
hal/input

###############################################################################
# Libraries
###############################################################################

mkdir -p \
libraries/libc \
libraries/libm \
libraries/libgui \
libraries/libnb \
libraries/libgfx \
libraries/libfs \
libraries/libnet

###############################################################################
# Userland
###############################################################################

mkdir -p \
userland/init \
userland/shell \
userland/services \
userland/desktop \
userland/apps \
userland/bin \
userland/sbin \
userland/lib

###############################################################################
# Desktop
###############################################################################

mkdir -p \
userland/desktop/compositor \
userland/desktop/window_manager \
userland/desktop/panel \
userland/desktop/filemanager \
userland/desktop/settings \
userland/desktop/themes \
userland/desktop/icons

###############################################################################
# Configuration
###############################################################################

mkdir -p \
etc/dinit.d \
etc/config \
etc/fonts \
etc/themes

###############################################################################
# SDK
###############################################################################

mkdir -p \
sdk/include \
sdk/examples \
sdk/templates \
sdk/tools

###############################################################################
# Development
###############################################################################

mkdir -p \
tests/kernel \
tests/drivers \
tests/filesystem \
tests/gui

mkdir -p \
docs/design \
docs/api \
docs/kernel \
docs/drivers \
docs/filesystem

###############################################################################
# Build
###############################################################################

mkdir -p \
build/bin \
build/obj \
build/rootfs \
build/iso

###############################################################################
# Output
###############################################################################

mkdir -p \
out/images \
out/releases \
out/logs

###############################################################################
# Placeholder files
###############################################################################

touch \
README.md \
LICENSE \
CHANGELOG.md \
Makefile \
builder.sh

echo
echo "NeoBench directory structure created successfully."
