#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$ROOT"

echo "========================================"
echo " Creating NeoBench Developer Tools"
echo "========================================"

###############################################################################
# Directories
###############################################################################

mkdir -p tools
mkdir -p tools/bin
mkdir -p tools/debugger
mkdir -p tools/disassembler
mkdir -p tools/profiler
mkdir -p tools/objdump
mkdir -p tools/readelf
mkdir -p tools/package
mkdir -p tools/image
mkdir -p tools/installer
mkdir -p tools/symbols
mkdir -p tools/scripts
mkdir -p tools/tests
mkdir -p tools/docs

###############################################################################
# Debugger
###############################################################################

mkdir -p tools/debugger/{src,include,tests,docs}

touch tools/debugger/src/main.c
touch tools/debugger/src/breakpoint.c
touch tools/debugger/src/memory.c
touch tools/debugger/src/registers.c
touch tools/debugger/src/disasm.c
touch tools/debugger/include/debugger.h
touch tools/debugger/Makefile

###############################################################################
# Disassembler
###############################################################################

mkdir -p tools/disassembler/{src,include}

touch tools/disassembler/src/main.c
touch tools/disassembler/src/m68k.c
touch tools/disassembler/include/disassembler.h
touch tools/disassembler/Makefile

###############################################################################
# ELF Inspector
###############################################################################

mkdir -p tools/readelf/{src,include}

touch tools/readelf/src/main.c
touch tools/readelf/src/elf.c
touch tools/readelf/include/readelf.h
touch tools/readelf/Makefile

###############################################################################
# Object Dump
###############################################################################

mkdir -p tools/objdump/{src,include}

touch tools/objdump/src/main.c
touch tools/objdump/src/objdump.c
touch tools/objdump/include/objdump.h
touch tools/objdump/Makefile

###############################################################################
# Profiler
###############################################################################

mkdir -p tools/profiler/{src,include}

touch tools/profiler/src/main.c
touch tools/profiler/src/profile.c
touch tools/profiler/include/profiler.h
touch tools/profiler/Makefile

###############################################################################
# Image Tools
###############################################################################

mkdir -p tools/image/{src,include}

touch tools/image/src/main.c
touch tools/image/src/image.c
touch tools/image/include/image.h
touch tools/image/Makefile

###############################################################################
# Package Manager
###############################################################################

mkdir -p tools/package/{src,include}

touch tools/package/src/main.c
touch tools/package/src/package.c
touch tools/package/include/package.h
touch tools/package/Makefile

###############################################################################
# Installer
###############################################################################

mkdir -p tools/installer/{src,include}

touch tools/installer/src/main.c
touch tools/installer/src/install.c
touch tools/installer/include/installer.h
touch tools/installer/Makefile

###############################################################################
# Documentation
###############################################################################

touch tools/docs/README.md
touch tools/docs/DEBUGGER.md
touch tools/docs/PACKAGING.md
touch tools/docs/IMAGE_TOOLS.md

###############################################################################
# Tests
###############################################################################

touch tools/tests/test_debugger.c
touch tools/tests/test_profiler.c
touch tools/tests/test_readelf.c

echo
echo "Developer tools created."

find tools | sort
