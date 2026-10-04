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
  NeoBench 0.1.7 m68k-aga
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
>dev n=5
>dev ata.device u=1 b=0 s=512 io=read,write
>dev atapi.device u=1 b=1 s=2048 io=read
>dev sound.device u=1 b=0 s=0 io=none
>dev input.device u=1 b=0 s=0 io=none
>dev serial.device u=1 b=0 s=0 io=none
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
same. A CD is taken at its word in the cylinder registers (`$14/$EB`), read
with ATAPI READ(10) in 2048-byte blocks, and asked for in one piece: `$FFFF`
is written back to those registers before every PACKET, because left with a
disk command's address in them the device answers a sector in byte-sized
phases that a transfer reading ahead blind walks into. `sdcard.device` is
the same bus one unit along; `sound.device` is Paula; `input.device` is the
keyboard on CIA-A with the port on Paula and `serial.device` Paula's serial
line — no card to find, so both are bound from the moment the machine came
up; `zz9000.device` binds only to a card that was actually found. A driver
with no sector path reports `io=none` rather than passing itself off as a
disk.

One bus carries a host with no expansion card behind it: the A4000T
motherboard puts an NCR53C710 in the Gayle window, so `scsi.device` looks for
the chip rather than for the machine. CTEST1 has to answer `$F0` first and a
scratch register has to round trip `$5A`/`$A5`, both before a single byte is
written, and the register numbers are the board's rather than the CPU's —
register R decodes at `$DD0040 + (R xor 3)`, mirrored at `$DD0080`, which is
why CTEST1 reads at `$DD0056` and why nothing below `$DD0040` is ever touched.
Past identification every command is a SCRIPTS program in `.bss`: the chip
fetches its instructions by address and this ROM is not writable, so select
with ATN, identify, the command, the data phase that command names, status,
message in and the transfer control `$98080000` are built per command and
handed over. The same disk sitting on that bus, on an A4000T:

```
>dev n=4
>dev scsi.device u=1 b=0 s=512 io=read,write
[  OK  ] scsi.device: UAE     install.hdf, 64 MB (target 0)
[  OK  ] block read: scsi.device sector 0 (512 bytes)
[  OK  ] 1 of 5 storage drivers bound
```

An A1200 has no such chip, and Gayle answers `$00` outside the IDE registers
and ignores the writes there, so both reads come back as nothing at all:
`[ WARN ] scsi.device: no NCR53C710 host in the Gayle window`, and the boot
carries on.

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

### Desktop

Workbench-3.2-shaped chrome over NeoBench's own backdrop: a list view that
reverses the chosen row, icons on a 24 px tile, and the start bar at half
height. The windows themselves are drawn in the Aero terms the bar and the
start menu already use — a pane of glass across the caption with the backdrop
still reading through it, a light steel rim, a shadow thrown below and to the
right, and Workbench's grey body and white field sunk inside it. The scene is
rebuilt from the flags and only the rows a change touched are presented, so
two windows standing over each other cost nothing; a window's body is opaque,
and only its caption is glass.

The start menu is cut from the same terms and stands in the bar it is opened
from: its foot is the bar's top edge, so it rises out of the furniture the orb
sits in rather than floating over the middle of the screen, and it throws no
shadow down there — nothing comes between the two. Its own name is set down a
band at the left edge rather than across a rule at the head, turned a quarter
so it reads down the strip with a divider beside it, and the chosen row lights
as the glass lights everything else in Aero. With `bar = classic` the same
menu is the flat Workbench field it was: grey panel, blue row, hard edge.

Every window can be picked up and carried: press the caption — anywhere in
the strip that is not one of its three buttons — and it goes where the
pointer goes until the button comes back up, taking the keyboard with it on
the way, as a press inside a window always does. The clamp is on the caption
rather than the frame, so a window may stand half off an edge, as windows do
on any desktop, but enough of the strip stays on the screen to be picked up
by again. All six move: the four program windows by their captions, the dial
and the monitor by their own bodies.

Six programs, and the start menu names three of them: Files, which is a drawer
above the rule, and **About** and **Preferences**, the two rows below it. Both
sections are set in alphabetical order, because a menu is a list the eye runs
down looking for one name — the drawers run Bench, Core, Docs, Files, Home,
Media, and the rows under the rule run **About**, which says what this build
is, then **Preferences**, which changes how the desktop sits. Files opens the
browser on the root of the store rather than sitting below the rule, because
it is the drawer every other one is looked for in. The other three are filed
where they belong rather than listed: Clock, Monitor and **NeoText** in
`Tools/`, and **Preferences** also stands in `Config/` beside the files it
changes. Clock is an analogue dial; Monitor
shows fast memory in use; and NeoText opens any file in the store — plain
text as written, a PDF with its text lifted out of the page, anything else
as a hex view of the bytes it is, eight to a line.

PDF text is read here rather than borrowed (`boot/rom/pdf.c`): page streams
are inflated by `boot/rom/inflate.c`, a raw DEFLATE decoder with no zlib
header, no allocation and no recursion, and the text operators are walked so
the lines of the page come out as the lines of the reader — `Tj`, `TJ`, `'`
and `"` show their strings, `T*` and `Td`/`TD` and a `Tm` that moves down the
page put the breaks back, two moves in a row give the blank line between
paragraphs, and kerning narrow enough to be a space becomes one. Image and
embedded-font streams are skipped rather than decoded; a document set through
a font NeoBench cannot read falls back to the hex view instead of showing
nonsense. `system/Core/Docs/guide.pdf` ships as a document to open.

```text
text  row 12 of 84                    (store is read-only)
```

`backdrop = wash|paper|azure|dusk|slate` in `Config/screen.cfg` says which of
the five the machine comes up with, and the Preferences pane chooses one for
the session and repaints at once. The five are five hues of one light field,
so the mark, the wordmark, the horizon glows and the chrome over them read
the same on every one.

`font = Xen|Xen11|System` in the same file sets the face — Xen at nine rows,
Xen11 the same face cut at eleven for a desk that wants its type a size
bigger, System the eight pixel console face kept for comparison. It is the
one face every window, menu, icon label and page of the reader sets its type
in, and it is file-only like most of that file: the face answers its own
height and the line it stands to the next, so nothing has to be measured
twice for it.

The pane is filed where those files are. `Config/Preferences` is a program
entry in the store — a file whose first line reads `program = preferences` —
so opening Files on `Config/` lists it beside `screen.cfg` and `pointer.cfg`
with `run` where a document would show its size, wearing the same rose tile
it wears in the start menu, and choosing it starts the pane rather than the
reader. Any directory can carry an entry this way; a file that says nothing
of the kind still opens in NeoText, so a typo costs the shortcut and nothing
else.

The tools drawer holds the clock, the monitor and the reader: `Tools/Clock`
and `Tools/Monitor` read `program = clock` and `program = monitor`, and
`Core/Docs/About` reads `program = about`, so all three stand beside the
files they belong with — the toolbox for the two that read the machine, the
documentation for the panel that says what this is. Clock, Monitor and
NeoText are not on the start menu, so the drawer is how they are started;
About keeps its row there as the one program the list still carries. `Tools/`
carries a note saying what is in it, and takes NeoShell and the calculator as
they arrive. A document chosen in Files still opens in NeoText whatever else
is filed here. Seven rows fit in Files, because the root carries six
drawers.

The keyboard follows the window you are in: the cursor keys walk the list
that is standing or move the caret, Return opens what is lit or starts a new
line, and Escape puts down whichever program has the keyboard. Tab walks the
keyboard round the programs that are running, in the order the task row shows
them and wrapping at the end; Ctrl with a cursor key carries the window the
keyboard is in by eight pixels, against the same wall as the drag; and either
cursor key with Shift held pages the reader, which is the key the keymap has
no Page Up or Page Down for. The start menu keeps all three while it stands,
as it keeps the cursor keys. A press on a task button gives that window the
keyboard, and a press on the button of the window that is already lit puts it
down.

## Building

The ROM is freestanding m68k code — no libc, no libgcc (no runtime division:
digit formatting uses a subtraction table, 8-bit scaling uses a multiply-shift).

```sh
make -C boot/rom
```

Requires an m68k cross GCC (`m68k-linux-gnu-gcc` / `ld` / `objcopy`, or the
bebbo toolchain on `PATH`). Output: `boot/rom/neobench.rom`, a 512 KB image
vectors at 0, runnable as a Kickstart ROM replacement in an emulator.

To press the install disc:

```sh
make iso
```

Output: `images/NeoBench-0.1.7.iso` — the ROM rebuilt and its chainload
hunk linked after it, `system/` packed into `NBFS.IMG` by
`tools/mknbfs.py` (checked by `tools/nbfs/info/nbfs-info`), and the six
staged files pressed into a plain ISO 9660 level 1 volume by
`tools/mkiso.py`. Nothing but the cross GCC and `python3` is needed on the
host; like everything under `images/`, the disc itself is not tracked.

## The install disc

The disc is NeoBench's installer, and it is honest about what it is. In
sector 0, where a boot floppy keeps its boot block, sits NeoBench's own
`NBISO` installer block (signature, version, volume name); the ISO 9660
volume sits at sector 16 as it does on any other disc.

**It does not boot the machine, and does not claim to.** A desktop Amiga's
Early Startup Control lists hard disk partitions and floppies only — booting
from CD exists on the Amiga behind the CDTV and CD32 ROMs, which is not
where this runs. What happens instead: the ROM boots as it always does, the
ATAPI driver reads sector 0, finds `NBISO`, mounts the volume with
**NeoBench's own ISO 9660 reader** (`boot/rom/iso9660.c` — both-endian
fields honoured, L and M path tables, directory records walked without a
byte of libc) and runs the installer (`boot/rom/install.c`) from what the
disc carries. The boot medium and the install medium stay what they should
be: the ROM starts the system, the disc supplies the payload.

The installer's rule is blunt, because nobody is at the keyboard to be
asked. It reads the first sector of the first disk and:

| First disk sector | Verdict | Serial line |
| --- | --- | --- |
| all zeroes | stream `NBFS.IMG` onto it | `>install state=done secs=8192` |
| `NBBOOT` and a readable NBFS superblock | already installed, left alone | `>install state=present` |
| `NBBOOT`, superblock gone | damaged, left alone | `>install state=damaged` |
| anything else | somebody else's disk, left alone | `>install state=refused` |

There is no format path at all: only the sectors of the image are written.
Each chunk of the stream gets three attempts, is read back and compared, and
is then read a second time and compared again — `chk= mis= misrd= cdmis=
cd2rd=` close the final serial line, all zeros on a good run. The desktop
log carries the same story in English:

```text
[  OK  ] iso9660: NeoBench installer disc "NEOBENCH", 6 files
[  OK  ] installer: NeoBench written to hda (4096 KB)
```

`INSTALL.TXT` on the disc (`system/Core/Docs/install.txt`) says all of this
to the person installing, step by step.

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

One trap, if the disk pointed at has already been installed: a hardfile
whose first sector is neither blank nor an Amiga `RDSK` gets a *synthetic*
RDB pasted over it — FS-UAE answers reads under 256 KB from the fabrication
and shifts the rest of the file down, so a disk starting `NBBOOT` reads back
as `RDSK` and the installer refuses the very volume it wrote itself. Pin the
drive as an RDB device and the fabrication is never made:

```ini
hard_drive_0_type = rdb
```

A blank disk skips the trap either way, which is why the first install needs
nothing special.

The motherboard's SCSI bus takes the same kind of disk on a different
controller, and only an A4000T has one. FS-UAE has no `amiga_model = A4000T`
— the board is selected by its chipset compatibility instead:

```ini
uae_chipset_compatible = A4000T
hard_drive_0 = /path/to/install.hdf
hard_drive_0_type = rdb
hard_drive_0_controller = scsi0_a4000t
```

`scsi0_a4000t` names the channel and the target is its index, so unit 0 on
that controller is target 0, which is where `scsi.device` looks for it. The
`rdb` type is needed for the same reason as above, and the disk has to be a
file: the host takes a CD, a tape or a hard file, never a directory.

Swap `cpu =` between `68020`, `68030`, `68040` and `68060` to watch the CPU
detection ladder answer with the right model.

## Repository layout

| Path | Contents |
| --- | --- |
| `boot/rom/` | The boot ROM: reset code, chipset bring-up, font, hardware probes (`fline.S`, `probe.c`), linker script, Makefile |
| `kernel/` | Kernel core: entry (`kernel_main.c`), boot banner and detection log (`banner.c`), text console, drivers, filesystems |
| `user/` | Userland (coreutils and friends) |
| `system/` | The tree packed into the ROM and browsed by the desktop: `Apps/`, `Config/`, `Core/` (Bench, Docs, Media), `Home/` (Desktop, Documents, Music, Pictures, Videos), `Temp/` — where downloads land, because NeoBench has no RAM disk — and `Tools/`, the drawer of filed programs |
| `docs/` | Specifications (filesystem, ABI) |
| `tools/` | Host-side utilities (NBFS image tools, disassembler, …) |

## Roadmap

- [x] AGA hires 8bpp framebuffer and native text console
- [x] systemd-style boot log with real hardware probes (CPU/MMU/FPU/memory/RTG)
- [x] Chainload delivery from a genuine AmigaOS 3.2.3 boot: a Hunk
      executable run from `S:Startup-Sequence`, which arrives with the
      chipset programmed and the OS owning the vectors, is checked
      against the reserved chip ranges where it landed, and then takes
      the machine — vectors, stack, boot log and display
- [x] Futuristic-clean desktop scene at 640×512: procedural wallpaper with a
      faded logo watermark, an Aero taskbar of tinted glass over the backdrop
      (with `bar = classic` for the flat Workbench field), a start menu
      carrying an MUI-style icon set -- ramped tiles with a white rim,
      a highlight arc and the mark cut out in white, drawn at four
      sizes -- and the programs, opaque Workbench
      windows, and dial and monitor gadgets
- [x] NeoBench's own `.device` drivers — `ata.device`, `atapi.device`,
      `sdcard.device`, `scsi.device`, `sound.device`, `zz9000.device` —
      bound because the hardware answered, each saying what it can do
      (`io=read,write`, `io=read`, `io=none`), with a boot self-test that
      reads sector 0 of the disk and the volume header of a CD
- [x] Install disc: `make iso` presses `images/NeoBench-0.1.7.iso`, an
      ISO 9660 level 1 volume carrying `NBFS.IMG` as the payload with
      NeoBench's `NBISO` installer block in sector 0 — mounted off the
      ATAPI bus by NeoBench's own reader (the disc does not claim to
      boot a desktop Amiga, which cannot boot one) and streamed to a
      blank disk only, with installed, damaged and foreign disks left
      exactly as they are
- [x] Input handling and window management on top of the static scene:
      task-row buttons that raise as well as put down, Tab to walk the
      keyboard round the windows that are standing, and Ctrl with a cursor
      key to carry one

## Licence

See [LICENSE](LICENSE).
