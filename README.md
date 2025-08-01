# NeoBench ROM

## Overview
NeoBench ROM is a custom boot ROM with Amiga 4000 AGA features, 68060 CPU support, and a Vista-inspired desktop environment.

## Features
- **AGA Graphics Chipset Support**: Full 24-bit color, high resolution modes
- **68060 CPU Support**: 100MHz CPU emulation with MMU and FPU features
- **Vista-Style Desktop**: Modern UI with transparent effects, taskbar, and start menu
- **Multiple Backgrounds**: 6 custom desktop wallpapers with NeoBench branding
- **No Icons**: Clean desktop experience similar to Windows Vista

## Build Information
- ROM Size: Less than 512KB, optimized for Amiberry compatibility
- Build System: Enhanced GNU toolchain with custom linker script
- Memory Usage: Optimized for 16MB+ RAM systems

## Usage with Amiberry
1. Copy `build/neobench.rom` to your Amiberry configuration directory
2. Configure Amiberry with:
   - CPU: 68060 at 100MHz
   - Chipset: AGA (Advanced Graphics Architecture)
   - Memory: 16MB or more recommended
   - In ROM settings, point to your `neobench.rom` file

## Keyboard Controls
- **F1**: System Information
- **F2**: Open BIOS Settings
- **F10**: Toggle between the 6 desktop backgrounds
- **Win+D**: Show desktop (hide all windows)
- **Win+E**: Open file explorer
- **Alt+Tab**: Switch between windows

## Technical Notes
- **Boot Process**: Automatically detects bootable devices (SCSI, USB, Floppy)
- **Desktop Environment**: Windows Vista-inspired interface with AGA color capabilities
- **Memory Management**: Full 68060 MMU support for enhanced memory protection

Developed by NeoBench Team © 2025
