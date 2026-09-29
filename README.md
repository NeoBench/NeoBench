# NeoBench

A from-scratch operating system for classic Amiga hardware, targeting **AGA-only
machines (A1200 class) with a 68060 CPU**. The boot ROM chainloads after a genuine
AmigaOS 3.2.3 ROM — the original ROM stays installed, boots first, and hands
control to NeoBench.

## Status

What works today, on real hardware or the FS-UAE/WinUAE emulator:

- **Native AGA video from the first instruction** — hires 640×512 interlaced,
  8 bitplanes (256 colours), driven directly by the chipset. No ROM calls, no OS
  underneath.
- **Linux-style boot console** — a systemd-style detection log with the P1
  phosphor green (`#33FF33`) palette and an upright 80×32 bitmap console:

  ```
  NeoBench 0.1.3 m68k-aga
  [  OK  ] CPU detected (Motorola 68060)
  [  OK  ] RTG detected (hires 640x512 lace, 8 bpp)
  [  OK  ] MMU detected
  [  OK  ] FPU detected
  [  OK  ] Memory detected (80 MB fast, 80 MB preferred)
  [  OK  ] UART detected (9600 baud)
  [  OK  ] System detected.
  ```

- **Every line is a real probe**, not an assumption:
  - *CPU* — no CPU ID register exists on m68k, so the model falls out of two
    instruction probes: `MOVE16` (68040/68060 only), then `MOVEC` of the PCR
    (a 68060-only control register — a 68040 raises illegal instruction, on
    silicon as well as in emulation). The MMU probe's 68030 `PFLUSHA` form
    settles 68020 vs 68030. NeoBench is 68060-only, so anything else prints a
    red `[FAILED]` line naming the CPU that was actually found.
  - *MMU / FPU* — by executing an instruction that only exists when the feature
    does (`PFLUSHA`, `FNOP`) and catching the trap when it doesn't. A 68060
    powers up with its FPU disabled, so the boot clears PCR bit 1 first.
  - *Memory* — write/read patterns at the last long of each megabyte of
    expansion space, then above the chipset window once the low map has filled;
    the first gap ends the count. The result is reported as **fast** RAM
    because that is what the startup requirement is written against:
    `[FAILED]` below the 50 MB floor, `[ WARN ]` below the 80 MB preferred.
  - *RTG / UART* — framebuffer readback with pixel restore; the serial port's
    presence is asserted by the console that has been using it since reset.
  - All instruction traps are caught by a temporary exception shim (vectors 4,
    11 and 61) that rewrites the saved PC to a recovery label — a negative
    result is a caught trap, never a fault-stub halt.

Verified generation by generation in FS-UAE: 68060 reports green `CPU
detected (Motorola 68060)`; 68040/68030/68020 each report red `CPU is not
68060 (Motorola 680x0)` and boot continues.

## Building

The ROM is freestanding m68k code — no libc, no libgcc (no runtime division:
digit formatting uses a subtraction table, 8-bit scaling uses a multiply-shift).

```sh
make -C boot/rom
```

Requires an m68k cross GCC (`m68k-linux-gnu-gcc` / `ld` / `objcopy`, or the
bebbo toolchain on `PATH`). Output: `boot/rom/neobench.rom`, a 512 KB image
vectors at 0, runnable as a Kickstart ROM replacement in an emulator.

## Testing with FS-UAE

Point a config at the built ROM:

```ini
amiga_model = A1200
chipset = aga
kickstart_file = /path/to/NeoBench/boot/rom/neobench.rom
floppy_drive_0 = 0
fullscreen = 1

cpu = 68060                    # FS-UAE's option is "cpu" — "cpu_type" is WinUAE-internal and silently ignored
fast_memory = 8
uae_fastmem_autoconfig = false # fast RAM for a bare ROM: no OS runs Zorro autoconfig to map it
```

Two gotchas both cost a debugging session each:

1. `cpu = 68060` — the FS-UAE frontend reads `cpu`; `cpu_type` never reaches
   the core and the A1200 preset quietly stays at a 68020.
2. `uae_fastmem_autoconfig = false` — fast memory is otherwise a Zorro II
   autoconfig board, mapped only when an OS enumerates it. NeoBench probes
   memory directly, so the direct-map mode is what makes it visible.

Swap `cpu =` between `68020`, `68030`, `68040` and `68060` to watch the CPU
detection ladder answer with the right model.

## Repository layout

| Path | Contents |
| --- | --- |
| `boot/rom/` | The boot ROM: reset code, chipset bring-up, font, hardware probes (`fline.S`, `probe.c`), linker script, Makefile |
| `kernel/` | Kernel core: entry (`kernel_main.c`), boot banner and detection log (`banner.c`), text console, drivers, filesystems |
| `user/` | Userland (coreutils and friends) |
| `system/` | The tree packed into the ROM and browsed by the desktop: `Apps/`, `Config/`, `Core/` (Bench, Docs, Media), `Home/` (Desktop, Documents, Music, Pictures, Videos) and `Temp/` — where downloads land, because NeoBench has no RAM disk |
| `docs/` | Specifications (filesystem, ABI) |
| `tools/` | Host-side utilities (NBFS image tools, disassembler, …) |

## Roadmap

- [x] AGA hires 8bpp framebuffer and native text console
- [x] systemd-style boot log with real hardware probes (CPU/MMU/FPU/memory/RTG)
- [ ] Chainload delivery from a genuine AmigaOS 3.2.3 boot (bootblock vs.
      `S:Startup-Sequence` hunk executable)
- [x] Futuristic-clean desktop scene at 640×512: procedural wallpaper with a
      faded logo watermark, a custom icon set, dark glass windows, dial and
      monitor gadgets, and a taskbar with the mark-only start orb
- [ ] Input handling and window management on top of the static scene

## License

See [LICENSE](LICENSE).
