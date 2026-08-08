#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "========================================"
echo "Creating NBFS Development Tools"
echo "========================================"

mkdir -p tools/nbfs
mkdir -p tools/nbfs/include
mkdir -p tools/nbfs/lib

###############################################################################
# Source Files
###############################################################################

TOOLS=(
mkfs.nbfs
fsck.nbfs
mount.nbfs
umount.nbfs
dump.nbfs
debug.nbfs
label.nbfs
tune.nbfs
bench.nbfs
stat.nbfs
)

for TOOL in "${TOOLS[@]}"
do
cat > "tools/nbfs/${TOOL}.c" <<EOF
/*
 * ${TOOL}
 * NeoBench File System Utility
 */

#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("${TOOL}\\n");
    printf("Not implemented yet.\\n");

    return 0;
}
EOF
done

###############################################################################
# Shared Header
###############################################################################

cat > tools/nbfs/include/nbfs_tool.h <<EOF
#ifndef NBFS_TOOL_H
#define NBFS_TOOL_H

#define NBFS_VERSION "1.0"

#endif
EOF

###############################################################################
# README
###############################################################################

cat > tools/nbfs/README.md <<EOF
# NBFS Utilities

Utilities included:

- mkfs.nbfs
- fsck.nbfs
- mount.nbfs
- umount.nbfs
- dump.nbfs
- debug.nbfs
- label.nbfs
- tune.nbfs
- bench.nbfs
- stat.nbfs
EOF

###############################################################################
# Makefile
###############################################################################

cat > tools/nbfs/Makefile <<'EOF'
CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude

TOOLS = \
mkfs.nbfs \
fsck.nbfs \
mount.nbfs \
umount.nbfs \
dump.nbfs \
debug.nbfs \
label.nbfs \
tune.nbfs \
bench.nbfs \
stat.nbfs

all: $(TOOLS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TOOLS)
EOF

echo
echo "NBFS tool framework created."
