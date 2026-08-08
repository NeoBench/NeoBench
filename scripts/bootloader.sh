#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "Creating NeoBench Boot System..."

###############################################################################
# Directories
###############################################################################

mkdir -p boot
mkdir -p boot/include
mkdir -p boot/loader
mkdir -p boot/platform
mkdir -p boot/rom

###############################################################################
# Boot Assembly
###############################################################################

cat > boot/start.S <<'EOF'
/*
 * NeoBench
 * Motorola 680x0 Startup
 */

.section .text
.align 4

.global _start
.extern kernel_main

_start:

    /* Supervisor Mode */

    move.w  #0x2700,%sr

    /* TODO
       Setup Stack
       Clear BSS
       Setup MMU
       Detect CPU
    */

    jsr kernel_main

1:
    bra.s 1b

EOF

###############################################################################
# Linker
###############################################################################

cat > boot/linker.ld <<'EOF'
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00100000, LENGTH = 64M
}

SECTIONS
{
    .text :
    {
        *(.text*)
    } > RAM

    .rodata :
    {
        *(.rodata*)
    } > RAM

    .data :
    {
        *(.data*)
    } > RAM

    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM
}
EOF

###############################################################################
# Boot Header
###############################################################################

cat > boot/include/boot.h <<'EOF'
#ifndef NB_BOOT_H
#define NB_BOOT_H

void boot_init(void);
void boot_banner(void);

#endif
EOF

###############################################################################
# Boot C
###############################################################################

cat > boot/boot.c <<'EOF'
#include "include/boot.h"

void boot_init(void)
{

}

void boot_banner(void)
{

}
EOF

###############################################################################
# NeoLoader Stub
###############################################################################

cat > boot/loader/neoloader.c <<'EOF'
#include "../include/boot.h"

void neoloader_start(void)
{

}
EOF

###############################################################################
# README
###############################################################################

cat > boot/README.md <<'EOF'
NeoBench Boot System

Contains:

- Startup Assembly
- Linker Script
- NeoLoader
- ROM Support
- Early Hardware Init
EOF

echo
echo "Boot subsystem created."
