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
  NeoBench 0.1.6 m68k-aga
  [  OK  ] CPU detected (Motorola 68060)
  [  OK  ] RTG detected (hires 640x512 lace, 8 bpp)
  [  OK  ] MMU detected
  [  OK  ] FPU detected
  [  OK  ] Memory detected (136 MB fast, 136 MB preferred)
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
  - *Memory* — two different patterns written to the last two longs of each
    megabyte, each of which has to survive the other's write: a single pass
    hands back whatever the bus last carried and calls a hole RAM. The low
    map, `$200000` up, ends at its first gap; above the chipset window the
    whole map is walked instead, because a Blizzard board answers at
    `$48000000` past a gigabyte of nothing, and each megabyte is counted once
    — a second window onto the same bank is a mirror, not more memory. The
    result is reported as **fast** RAM because that is what the startup
    requirement is written against: `[FAILED]` below the 128 MB floor,
    `[ WARN ]` below the 136 MB preferred.
  - *RTG / UART* — framebuffer readback with pixel restore; the serial port's
    presence is asserted by the console that has been using it since reset.
  - *Zorro / ZZ9000* — a read-only walk of the autoconfig space at `$E80000`
    and `$FF000000`, one 64-byte slot at a time, each checked for structure
    before it is believed and matched on manufacturer `$6D6E` with products
    3/4/5 before a single byte is written — there is no shutup write. FS-UAE
    drains the autoconfig chain at reset, so nothing is found there by design:
    this is proved by `tools/tests/test_zorro`, a host test over recorded slot
    images, together with an emulated Zorro III card decode. Whether a real
    ZZ9000 is still on the bus after a genuine AmigaOS 3.2.3 boot has **not**
    been established.
  - All instruction traps are caught by a temporary exception shim (vectors 4,
    11 and 61) that rewrites the saved PC to a recovery label — a negative
    result is a caught trap, never a fault-stub halt.

Verified generation by generation in FS-UAE: 68060 reports green `CPU
detected (Motorola 68060)`; 68040/68030/68020 each report red `CPU is not
68060 (Motorola 680x0)` and boot continues.

### Devices

NeoBench does not borrow AmigaOS's drivers. The `.device` layer is NeoBench's
own: a device is in the table because the hardware it names answered, and it
says plainly what it can do.

```
>dev n=3
>dev ata.device u=1 b=0 s=512 io=read,write
>dev atapi.device u=1 b=1 s=2048 io=read
>dev sound.device u=1 b=0 s=0 io=none
[  OK  ] ata.device: Gayle IDE controller (PIO mode 0)
[  OK  ]   hda: UAE-IDE New Disk.hdf, 1024 MB
[  OK  ] atapi.device: AU-ETAPA I
[  OK  ] block read: ata.device sector 0 (512 bytes)
[  OK  ] block read: atapi.device sector 16 (2048 bytes)
```

`ata.device` and `atapi.device` drive Gayle in PIO mode 0 — the task file
programmed directly, DRQ polled under a bound, the data register moved a byte
at a time so a buffer at an odd address is legal. Nothing on the bus is taken
on trust: a unit is selected before its status is believed, because status
belongs to the *selected* unit, and a driver that asks before it selects has
already decided "nothing here" about a device that is perfectly well there —
which is how a CD that is still coming ready becomes an empty bus. The word
order is measured rather than guessed: the first sector of a rigid disk starts
`RDSK`, and reading it the other way hands the caller `DRKS` — close enough to
look plausible, and wrong in a way nothing downstream would survive. IDENTIFY
is decoded in the byte order word 0 pins down (`$0040` disk, `$848A` card,
`$80C0`-style packet) rather than by asking which reading of the model name
looks like text, which cannot settle it: exchanging the bytes inside a field
leaves its count of letters exactly where it was, so both readings score the
same. A CD is taken at its word in the cylinder registers (`$14/$EB`) and read
with ATAPI READ(10) in 2048-byte blocks; `sdcard.device` is the same bus one
unit along; `sound.device` is Paula; `zz9000.device` binds only to a card that
was actually found. A driver with no sector path reports `io=none` rather than
passing itself off as a disk.

Every path is bounded — a missing, slow or wedged unit answers "no" under a
poll bound and the boot carries on. The one thing never done at boot is a
write: with the byte order wrong there, sector 0 of the boot disk would be
corrupted. That path was proved once against a scratch file — a pattern
written, read back, and compared against the file as it sat on the host — and
is deliberately left out of the shipped self-test.

### Input

Three sources feed one queue of 64 keys (`boot/rom/kbd.c`), each answering for
itself, and each key is logged by source, raw code and what it decoded to — so
a key that does nothing says which of the three never sent it.

```
>kbd cia-a=$bfec01 ack=pulse queue=64 recv=kbd,ser
[  OK  ] input.device: Amiga keyboard on CIA-A, port on Paula
[ WARN ] usb.device: no host controller (AGA has no PCI bus; Zorro: 0 cards)
>key src=kbd raw=$4d got=$0101 name=down
>key src=kbd raw=$44 got=$0104 name=ret
>key src=ser raw=$61 got=0061 ch=a
```

The keyboard is read from CIA-A's serial data register, split into code and
break (bit 0), and acknowledged the way the hardware asks: `$00` in the SDR,
SPMODE set for a bounded spin, then cleared — with CRA's other bits read back
first so Timer A is not disturbed. One code is outstanding at a time, which is
what the protocol's mode 0 requires. CIA-A's serial interrupt is masked in at
init and polled through the ICR, which returns the flags and clears them — so
a level is checked rather than an edge counted. Serial bytes arrive from Paula
one at a time; `\r \n \t`, Escape, backspace and delete are given a key
meaning and everything else is what it says.

The USB HID parser (`boot/rom/hid.c`) is the boot-protocol half — eight bytes,
six key slots, rollover handled — with no hardware dependency at all, which is
the whole reason it can be tested: `tools/tests/test_hid` runs it on the host.
On AGA it is unreachable by construction, since AGA has no PCI bus and
`nb_usb_probe()` reports the Zorro walk it made instead. `nb_kbd_usb()` is the
join a host controller would call, and is documented as never called on AGA.

The keymap is the ROM's USA0 default plus the British national keys — `#` and
`~` on the key beside Return, `\` and `|` beside Left Shift — because those are
the keys a British keyboard has that USA0 does not draw. Caps Lock flips
letters only; the keypad parens have no Amiga key to press and answer nothing.

Keys reach the desktop through `nb_desktop_key()`: the Amiga keys open the
start menu as the orb does, Escape dismisses the menu before it dismisses a
selection, Up and Down walk the standing list, and Return activates what is lit
through the same call a double press makes — one path to an activation.

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

Three gotchas, each of which cost a debugging session:

1. `cpu = 68060` — the FS-UAE frontend reads `cpu`; `cpu_type` never reaches
   the core and the A1200 preset quietly stays at a 68020.
2. `uae_fastmem_autoconfig = false` — fast memory is otherwise a Zorro II
   autoconfig board, mapped only when an OS enumerates it. NeoBench probes
   memory directly, so the direct-map mode is what makes it visible.
3. The cursor keys arrive as joystick directions, not as keys: FS-UAE's
   built-in keyboard-joystick device claims them by default, so Up/Down never
   reach the emulated keyboard — letters, Return and Escape arrive fine and
   only the arrows are silent. Bind them back to the Amiga cursor keys:

   ```ini
   keyboard_key_up = action_key_cursor_up
   keyboard_key_down = action_key_cursor_down
   keyboard_key_left = action_key_cursor_left
   keyboard_key_right = action_key_cursor_right
   ```

To put something on the IDE bus for the device layer to find:

```ini
hard_drive_0 = /path/to/disk.hdf
hard_drive_0_controller = ide0
cdrom_drive_0 = /path/to/disc.iso
cdrom_drive_0_controller = ide0
```

The image goes in `cdrom_drive_0`: `cdrom_image_0` on its own is read by the
frontend and never reaches the drive, so the bus reports an empty unit. The
drive then arrives as the second unit on the channel the disk is on, which is
where Gayle can reach it — and `[  OK  ] block read: atapi.device sector 16`
is the ISO 9660 volume descriptor coming off it.

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
- [x] Chainload delivery from a genuine AmigaOS 3.2.3 boot: a Hunk
      executable run from `S:Startup-Sequence`, which arrives with the
      chipset programmed and the OS owning the vectors
- [x] Futuristic-clean desktop scene at 640×512: procedural wallpaper with a
      faded logo watermark, an Aero taskbar of tinted glass over the backdrop
      (with `bar = classic` for the flat Workbench field), a start menu
      carrying an MUI-style icon set -- ramped tiles with a white rim,
      a highlight arc and the mark cut out in white, drawn at four
      sizes -- and the programs, opaque Workbench
      windows, and dial and monitor gadgets
- [x] NeoBench's own `.device` drivers — `ata.device`, `atapi.device`,
      `sdcard.device`, `sound.device`, `zz9000.device` — bound because the
      hardware answered, each saying what it can do (`io=read,write`,
      `io=read`, `io=none`), with a boot self-test that reads sector 0 of
      the disk and the volume header of a CD
- [ ] Input handling and window management on top of the static scene

## Licence

See [LICENSE](LICENSE).
