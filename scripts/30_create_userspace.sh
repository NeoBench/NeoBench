#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench User Space"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p user
mkdir -p user/include
mkdir -p user/lib
mkdir -p user/bin
mkdir -p user/sbin
mkdir -p user/init
mkdir -p user/shell
mkdir -p user/coreutils
mkdir -p user/system
mkdir -p user/services
mkdir -p user/gui
mkdir -p user/tests
mkdir -p user/docs

###############################################################################
# Shell
###############################################################################

touch user/shell/main.c
touch user/shell/parser.c
touch user/shell/builtins.c
touch user/shell/history.c
touch user/shell/completion.c
touch user/shell/Makefile

###############################################################################
# Init
###############################################################################

touch user/init/main.c
touch user/init/service.c
touch user/init/config.c
touch user/init/Makefile

###############################################################################
# Core Utilities
###############################################################################

UTILS=(
cat
cp
date
df
echo
env
false
free
help
hostname
kill
ln
ls
mkdir
mount
mv
nbfsinfo
poweroff
ps
pwd
reboot
rm
rmdir
sleep
sync
touch
true
uname
uptime
whoami
)

for util in "${UTILS[@]}"; do
    mkdir -p "user/coreutils/$util"
    touch "user/coreutils/$util/main.c"
    touch "user/coreutils/$util/Makefile"
done

###############################################################################
# System Libraries
###############################################################################

LIBS=(
libc
libm
libnbfs
libgui
libipc
libsys
)

for lib in "${LIBS[@]}"; do
    mkdir -p "user/lib/$lib"
    touch "user/lib/$lib/Makefile"
    touch "user/lib/$lib/README.md"
done

###############################################################################
# GUI
###############################################################################

mkdir -p user/gui/desktop
mkdir -p user/gui/window_manager
mkdir -p user/gui/compositor
mkdir -p user/gui/file_manager
mkdir -p user/gui/terminal

touch user/gui/desktop/main.c
touch user/gui/window_manager/main.c
touch user/gui/compositor/main.c
touch user/gui/file_manager/main.c
touch user/gui/terminal/main.c

###############################################################################
# Documentation
###############################################################################

touch user/docs/README.md
touch user/docs/ABI.md
touch user/docs/SYSCALLS.md

###############################################################################
# Top-level Makefile
###############################################################################

cat > user/Makefile <<'EOF'
SUBDIRS = \
shell \
init

all:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d; \
	done

clean:
	@for d in $(SUBDIRS); do \
		$(MAKE) -C $$d clean; \
	done

.PHONY: all clean
EOF

echo
echo "User space created."

find user | sort
