#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench SDK"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p sdk
mkdir -p sdk/bin
mkdir -p sdk/include
mkdir -p sdk/include/neobench
mkdir -p sdk/include/sys
mkdir -p sdk/include/gui
mkdir -p sdk/include/nbfs
mkdir -p sdk/lib
mkdir -p sdk/examples
mkdir -p sdk/templates
mkdir -p sdk/tools
mkdir -p sdk/docs
mkdir -p sdk/tests

###############################################################################
# Common headers
###############################################################################

HEADERS=(
errno.h
fcntl.h
limits.h
signal.h
stddef.h
stdint.h
stdio.h
stdlib.h
string.h
time.h
unistd.h
)

for f in "${HEADERS[@]}"; do
    touch "sdk/include/$f"
done

###############################################################################
# NeoBench API
###############################################################################

NB_HEADERS=(
application.h
desktop.h
event.h
ipc.h
process.h
service.h
thread.h
window.h
)

for f in "${NB_HEADERS[@]}"; do
    touch "sdk/include/neobench/$f"
done

###############################################################################
# GUI
###############################################################################

GUI_HEADERS=(
button.h
canvas.h
dialog.h
font.h
icon.h
image.h
label.h
layout.h
menu.h
widget.h
window.h
)

for f in "${GUI_HEADERS[@]}"; do
    touch "sdk/include/gui/$f"
done

###############################################################################
# System API
###############################################################################

SYS_HEADERS=(
fs.h
memory.h
mount.h
process.h
scheduler.h
syscall.h
timer.h
)

for f in "${SYS_HEADERS[@]}"; do
    touch "sdk/include/sys/$f"
done

###############################################################################
# NBFS API
###############################################################################

NBFS_HEADERS=(
bitmap.h
directory.h
inode.h
journal.h
nbfs.h
superblock.h
)

for f in "${NBFS_HEADERS[@]}"; do
    touch "sdk/include/nbfs/$f"
done

###############################################################################
# Libraries
###############################################################################

LIBS=(
libc.a
libgui.a
libipc.a
libm.a
libnbfs.a
libsys.a
)

for f in "${LIBS[@]}"; do
    touch "sdk/lib/$f"
done

###############################################################################
# Examples
###############################################################################

EXAMPLES=(
hello_world
hello_gui
filesystem
ipc_demo
window_demo
)

for ex in "${EXAMPLES[@]}"; do
    mkdir -p "sdk/examples/$ex"
    touch "sdk/examples/$ex/main.c"
    touch "sdk/examples/$ex/Makefile"
done

###############################################################################
# Templates
###############################################################################

mkdir -p sdk/templates/application
mkdir -p sdk/templates/library

touch sdk/templates/application/main.c
touch sdk/templates/application/Makefile

touch sdk/templates/library/library.c
touch sdk/templates/library/library.h
touch sdk/templates/library/Makefile

###############################################################################
# Documentation
###############################################################################

DOCS=(
API.md
ABI.md
BUILDING.md
STYLE_GUIDE.md
)

for f in "${DOCS[@]}"; do
    touch "sdk/docs/$f"
done

###############################################################################
# Tools
###############################################################################

TOOLS=(
nb-config
nb-new
nb-package
)

for t in "${TOOLS[@]}"; do
    touch "sdk/tools/$t"
    chmod +x "sdk/tools/$t"
done

###############################################################################
# Makefile
###############################################################################

cat > sdk/Makefile <<'EOF'
all:
	@echo "Building NeoBench SDK"

clean:
	@echo "Cleaning NeoBench SDK"

.PHONY: all clean
EOF

echo
echo "SDK created."

find sdk | sort
