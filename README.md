# NeoBench

NeoBench is a modern AmigaOS-like operating system for the Amiga 4000, built entirely in Rust, with custom ROMs, a new filesystem (NeoFS), splash screen (NeoSplash), and a terminal (NeoShell). It's designed to boot into a Vista-style desktop environment using MUI 5.0, with no icons, all downloads going to `Temp/`, and full compatibility with AmigaOS 3.2 ROMs.

## Directory Structure

- `/roms`: ROM builder configs and binaries
- `/tools/remus`: ROM building tool
- `/src`: Source code for OS modules
- `/os`: Final system layout (C, LIBS, DEVS, etc.)
- `/images`: NeoBench logos, splash screens
