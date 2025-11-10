# NeoBench

NeoBench is a custom Amiga-class desktop environment and ROM-based operating system designed for the NeoBench Rev 1 motherboard.  
The system targets a MC68060 (MMU + FPU), FPGA-based AGA replacement, SATA/SCSI boot, USB input, and full PCI/Zorro III expansion.

This repository contains the early-stage source code for the NeoBench ROM, tooling, assets, build scripts, and workspace generators.

---

## Features (In Development)

- **Full ROM-based boot system**
  - Custom bootloader written in Rust
  - Moving boot screen and animated logo
  - Startup/shutdown sound playback
  - Boot-selector UI (SATA/SCSI/USB)
  - DEL/TAB entry into advanced settings

- **Target Hardware**
  - NeoBench Rev 1 motherboard
  - MC68060 @ 100 MHz
  - FPGA AGA replacement with 8 MB ChipRAM
  - 4 GB onboard DDR memory
  - SATA + SCSI controllers
  - USB keyboard and mouse support
  - 7 PCI slots + 4 Zorro III slots

- **Memory Layout**
  - Entire OS executes from ROM
  - 8 MB ChipRAM fully addressable
  - PCI/Zorro memory windows
  - DDR-based Fast RAM

- **Future OS**
  - Custom NeoBench desktop environment
  - Rust core with Amiga-compatible subsystems
  - British English locale + date formatting
  - No Linux-style directory structure

---

## Repository Structure

