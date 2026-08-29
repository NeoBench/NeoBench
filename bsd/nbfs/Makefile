# NeoBench File System (NBFS) -- FreeBSD kernel module Makefile.
#
# This Makefile builds nbfs as a kernel module (KLD) for FreeBSD.
# It can be loaded via `kldload nbfs.ko` or linked into the kernel.
#
# Usage (standalone, with installed FreeBSD kernel sources):
#   make                    # build nbfs.ko
#   make install            # install to /boot/kernel/
#   kldload ./nbfs.ko      # load into running kernel
#
# When integrated into the NeoBench build, the configure script copies
# this file and all sources into the FreeBSD source tree under
# sys/fs/nbfs/ and the standard FreeBSD Makefile infrastructure
# handles compilation.

.SUFFIXES: .c .o

# Cross-compiler prefix for NeoBench m68k builds.  Empty for native.
CROSS_COMPILE ?=

CC      = ${CROSS_COMPILE}cc
CFLAGS  = -O2 -Wall -Wextra -Werror \
          -msoft-float \
          -ffreestanding -mno-red-zone \
          -I${.CURDIR} \
          -D_KERNEL \
          -DMODVERSION=0 \
          -include ${SYSDIR}/opt_global.h
LDFLAGS = -r

# If building inside the FreeBSD source tree, SYSDIR is set.
# Otherwise fall back to the standard location.
SYSDIR  ?= /usr/src/sys

# Source files.
SRCS    = nbfs_mount.c \
          nbfs_vnops.c \
          nbfs_node.c \
          nbfs_io.c \
          nbfs_crc32.c

OBJS    = ${SRCS:C/.c$/.o/}

# Module name.
KMOD    = nbfs

# For building as a standalone kernel module (KLD).
.if !target(all)
all: ${KMOD}.ko
.endif

${KMOD}.ko: ${OBJS}
	${CC} ${LDFLAGS} -o ${.TARGET} ${OBJS}

# When building inside the FreeBSD source tree, use the standard
# KLD infrastructure.
.if exists(${SYSDIR}/conf/kmod.mk)
.include <${SYSDIR}/conf/kmod.mk>
.endif

clean:
	rm -f ${OBJS} ${KMOD}.ko

install: ${KMOD}.ko
	${INSTALL} -m 555 ${KMOD}.ko ${DESTDIR}/boot/kernel/

.PHONY: all clean install
