# NeoBench Kernel v1.0.0 "Denise"

A bare-metal operating system kernel for classic Amiga hardware, written in C++ and M68K assembly.

```
    _   __            ____                  __  
   / | / /__  ____  / __ )___  ____  _____/ /_ 
  /  |/ / _ \/ __ \/ __  / _ \/ __ \/ ___/ __ \
 / /|  /  __/ /_/ / /_/ /  __/ / / / /__/ / / /
/_/ |_/\___/\____/_____/\___/_/ /_/\___/_/ /_/ 
```

## Features

### Kernel
- **68030/040/060 support** with CPU auto-detection at boot
- **MMU** with 4KB page tables (68030 PMMU, 68040/060 on-chip)
- **FPU** support (68881/68882 external, 68040/060 internal)
- **Cooperative multitasking** with round-robin scheduler
- **Memory management** — bitmap page allocator + slab allocator
- **TRAP-based syscall interface** (TRAP #0 through TRAP #7)
- **Full exception handling** with register dump on fault

### Display (NeoBench's Own Drivers)
- **ECS/OCS** — 640×256 hires, 80×32 text console, Copper-driven
- **AGA** — 640×480 with 256 colors, 24-bit palette, FMODE bandwidth
- **RTG** — Picasso II/IV, CyberVision 64 (Zorro autoconfig)
- Auto-detection via VPOSR/DENISEID register

### Hardware Drivers
- **Keyboard** — CIA-A SDR with full scancode table & modifiers
- **Mouse** — JOY0DAT counter + button state from CIA/POTGOR
- **Serial** — Custom chip UART (9600 default, debug console)
- **IDE/ATA** — A1200 Gayle + A4000 onboard, PIO mode
- **SCSI** — WD33C93 (A3000/A4000), bus scan + read/write
- **Audio** — Paula 4-channel, boot chime, beep
- **Network** — Zorro probe for A2065/Ariadne/X-Surf (stub)
- **Filesystem** — Amiga FFS/OFS read-only support

### NeoShell
Interactive command shell with:
- Line editing (arrows, Home/End, Delete, Backspace)
- Command history (Up/Down, 32 entries)
- Tab completion
- 24+ built-in commands: `ls`, `cd`, `cat`, `pwd`, `echo`, `ps`, `top`, `free`, `df`, `uname`, `neofetch`, `mount`, `reboot`, `halt`, and more
- Virtual filesystem: `/proc/`, `/dev/`, `/sys/`

### NeoCLI
System administration tool:
- `neocli status` — System dashboard
- `neocli benchmark` — CPU/FPU/Memory benchmark with ASCII bars
- `neocli mem dump <addr> <len>` — Hex memory dump
- `neocli mem test` / `cpu test` / `fpu test` — Hardware tests
- `neocli irq stats` / `dma stats` / `cache stats` — Hardware statistics
- `neocli config show/set` — Kernel configuration

### Boot Sequence
Linux-style boot with `[  OK  ]` status messages:
```
    _   __            ____                  __  
   / | / /__  ____  / __ )___  ____  _____/ /_ 
  /  |/ / _ \/ __ \/ __  / _ \/ __ \/ ___/ __ \
 / /|  /  __/ /_/ / /_/ /  __/ / / / /__/ / / /
/_/ |_/\___/\____/_____/\___/_/ /_/\___/_/ /_/ 
  Bare-Metal Amiga Kernel v1.0.0 (Denise)

[  OK  ] Detected CPU: Motorola 68040 @ 25MHz
[  OK  ] FPU: Internal 68040
[  OK  ] Chipset: AGA (Alice/Lisa)
[  OK  ] Chip RAM: 2048 KB
[  OK  ] Fast RAM: 64 MB at 0x08000000
[  OK  ] Memory Management Unit configured (4KB pages)
[  OK  ] Page allocator: 16384 Chip + 16384 Fast pages
[  OK  ] CIA Timer A: 50Hz system tick (PAL)
[  OK  ] Interrupt controller: levels 1-6 active
[  OK  ] Keyboard driver initialized (CIA-A SDR)
[  OK  ] Serial: 9600 8N1
[  OK  ] IDE: Seagate ST3120026A (120 GB)
[  OK  ] Filesystem: DH0: (FFS)
[  OK  ] Paula audio: 4 channels ready
[  OK  ] NeoShell v1.0 loaded
[  OK  ] NeoCLI v1.0 loaded
[  OK  ] Boot complete in 2.34 seconds

neobench:/> _
```

## Requirements

### Minimum Hardware
- **CPU:** Motorola 68030, 68040, or 68060 (with MMU)
- **FPU:** 68881/68882 (with 68030) or internal (68040/060)
- **RAM:** 10MB minimum (Chip + Fast combined)
- **Chipset:** OCS, ECS, or AGA (RTG cards also supported)
- **Storage:** IDE hard drive, SCSI, or floppy boot

### Tested Systems
- Amiga 1200 + Blizzard 1230/1240/1260
- Amiga 3000 (68030/25MHz)
- Amiga 4000/040
- Amiga 4000/060 (Apollo/Cyberstorm)

### Build Host
- Linux, macOS, or WSL2
- ~5GB disk space for toolchain build
- GCC build prerequisites (see install.sh)

## Quick Start

### 1. Install Cross-Compiler Toolchain

```bash
chmod +x install.sh
./install.sh
```

This builds:
- GCC 13.2.0 (C/C++) targeting m68k-elf
- GNU Binutils 2.41
- Newlib 4.3.0 (minimal C library)
- GDB 14.1 (remote debugging)
- ROM/ADF creation tools

### 2. Build the Kernel

```bash
# Source the NeoBench environment script from your install prefix
source <install-prefix>/neobench-env.sh

# Build for 68030 (default)
make

# Build for specific CPU
make CPU=68040
make CPU=68060

# Build for all CPUs
make build-all

# Size-optimized build
make CPU=68040 OPT=-Os

# Debug build
make DEBUG=1
```

If you already have an existing `m68k-elf` cross-compiler installed, run:

```bash
./install.sh --skip-toolchain
```

### 3. Create Bootable Media

```bash
# Create 512KB ROM image
make rom

# Create 1MB ROM image (for larger builds)
make rom1m

# Create bootable ADF floppy image
make adf
```

### 4. Run / Flash

#### Emulator (FS-UAE)
```ini
# fs-uae.conf
amiga_model = A1200
kickstart_file = build/neobench.rom
chip_memory = 2048
fast_memory = 65536
```

Or boot from ADF:
```ini
floppy_drive_0 = build/neobench.adf
```

#### Real Hardware — ROM Flash
1. Flash `build/neobench.rom` to a 512KB (27C400) or 1MB (27C800) EPROM
2. Install in your Amiga's Kickstart socket
3. Power on

#### Real Hardware — Floppy Boot
1. Write `build/neobench.adf` to a real floppy:
   ```bash
   # Linux with Catweasel/Kryoflux
   adf2disk build/neobench.adf /dev/fd0
   
   # Or use ADF-Copy on an Amiga with Workbench
   ```
2. Insert floppy and boot

#### Real Hardware — CF/SD Card
1. Write the binary directly to the boot sectors of a CF card
2. Use a CF-to-IDE adapter in your A1200/A4000

## Project Structure

```
neobench-kernel/
├── install.sh              # Cross-compiler toolchain installer
├── Makefile                # Build system
├── linker.ld               # Linker script (memory map)
├── README.md               # This file
│
├── boot/                   # M68K Assembly — startup & bootstrap
│   ├── hwdefs.inc          # Hardware register definitions
│   ├── vectors.S           # Exception vector table (256 vectors)
│   ├── start.S             # CPU/RAM detection, FPU init, jump to C++
│   └── loader.S            # Floppy bootblock loader
│
├── include/                # Headers
│   ├── neobench.h          # Master header (chip registers, API)
│   └── types.h             # Type definitions (uint32_t, etc.)
│
├── kernel/                 # Kernel core
│   ├── kernel.cpp          # Main kernel — boot sequence & init
│   ├── mmu.cpp             # MMU setup (68030/040/060 specific)
│   ├── fpu.cpp             # FPU init & context switch
│   ├── memory.cpp          # Page allocator + slab allocator
│   ├── process.cpp         # Cooperative multitasking
│   ├── interrupts.cpp      # Autovector interrupt handling
│   ├── timer.cpp           # CIA timer driver (system tick)
│   └── syscall.cpp         # TRAP #0 syscall dispatcher
│
├── drivers/                # NeoBench hardware drivers
│   ├── chipset/
│   │   ├── custom.h        # Custom chip helpers & macros
│   │   ├── ecs.cpp         # ECS/OCS display (80×32 text)
│   │   ├── aga.cpp         # AGA display (256 colors)
│   │   ├── rtg.cpp         # RTG framebuffer (Picasso/CyberVision)
│   │   └── display.cpp     # Display abstraction layer
│   ├── storage/
│   │   ├── ide.cpp         # IDE/ATA (Gayle + A4000)
│   │   ├── scsi.cpp        # SCSI (WD33C93)
│   │   └── ffs.cpp         # Amiga FFS/OFS filesystem
│   ├── input/
│   │   ├── keyboard.cpp    # CIA-A keyboard driver
│   │   ├── mouse.cpp       # Game port mouse driver
│   │   └── serial.cpp      # Serial port (debug console)
│   ├── audio/
│   │   └── paula.cpp       # Paula 4-channel audio
│   └── network/
│       └── net.cpp         # Network card probe (stub)
│
├── shell/                  # User interface
│   ├── neoshell.cpp        # NeoShell — interactive command shell
│   ├── neocli.cpp          # NeoCLI — system admin tool
│   └── console.cpp         # Console I/O & line editor
│
├── lib/                    # Support library
│   ├── string.h            # String function declarations
│   ├── string.cpp          # String/memory functions
│   └── printf.cpp          # kprintf / ksprintf
│
└── tools/                  # Build utilities (installed by install.sh)
    └── (neobench-makerom, neobench-makeadf)
```

## Architecture

### Memory Map (Runtime)
```
0x00000000 - 0x000003FF  Exception Vector Table (1KB)
0x00000400 - 0x000FFFFF  Kernel code + data + BSS (~512KB)
0x00100000 - 0x001FFFFF  Chip RAM heap (1MB, DMA accessible)
0x00200000 - 0x009FFFFF  Zorro II expansion (if present)
0x07000000 - 0x07FFFFFF  A3000/A4000 motherboard Fast RAM
0x08000000 - ...         Accelerator Fast RAM
0x40000000 - 0x7FFFFFFF  Zorro III expansion
0xBFD000   - 0xBFEFFF   CIA-A / CIA-B
0xDFF000   - 0xDFF1FF   Custom chip registers
0xF80000   - 0xFFFFFF   ROM space
```

### Syscall Interface
System calls via TRAP #0, number in D0, args in D1-D4:
| # | Syscall | Description |
|---|---------|-------------|
| 0 | SYS_EXIT | Exit process |
| 1 | SYS_WRITE | Write to fd |
| 2 | SYS_READ | Read from fd |
| 3 | SYS_OPEN | Open file |
| 4 | SYS_CLOSE | Close fd |
| 5 | SYS_ALLOC | Allocate memory |
| 6 | SYS_FREE | Free memory |
| 7 | SYS_FORK | Fork process |
| 8 | SYS_EXEC | Execute program |
| 9 | SYS_GETPID | Get process ID |
| 10 | SYS_SLEEP | Sleep (ms) |
| 11 | SYS_YIELD | Yield CPU |

## License

MIT License — Free for personal and commercial use.

## Credits

NeoBench was designed for the Amiga community. Long live the Amiga! 🖥️

