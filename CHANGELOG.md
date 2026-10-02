# Changelog

Notable changes to NeoBench, newest first. British English throughout.

## 0.1.7

### Installation

- **An install disc, pressed by `make iso`**: `images/NeoBench-0.1.7.iso`,
  five megs of plain ISO 9660 level 1 — `NBFS.IMG` (the system tree packed
  as an NBFS volume by `tools/mknbfs.py`), `NEOBENCH.EXE` (the chainload
  hunk), `NEOBENCH.ROM` and the three text files, with NeoBench's own
  `NBISO` installer block in sector 0 where a boot floppy keeps its boot
  block. The disc does not claim to boot a desktop Amiga, which cannot be
  coaxed into it: Early Startup Control lists hard disk partitions and
  floppies only, and CD boot lives behind the CDTV and CD32 ROMs. What
  happens instead is what `INSTALL.TXT` says: the ROM boots, the ATAPI
  driver reads sector 0, finds `NBISO`, and mounts the volume at sector 16
  with **NeoBench's own ISO 9660 reader** (`boot/rom/iso9660.c`) — primary
  volume descriptor, both-endian fields, L and M path tables, directory
  records that never cross a sector boundary, written against the spec
  rather than against a libc.
- **The installer writes only a blank disk** (`boot/rom/install.c`). A
  first sector of all zeroes is the only thing that invites the payload:
  `NBFS.IMG` streamed disc sector onto four disk sectors with the tail
  zero-padded, three attempts a chunk, each chunk read back and compared,
  then read a second time and compared again. `NBBOOT` plus a readable
  superblock reports the volume by name and is left alone, `NBBOOT` with
  the superblock gone reports a damaged volume and is left alone,
  anything else reports a refusal and is left alone — there is no format
  path in the code at all, only the sectors of the image. Every verdict
  lands on the serial line as `>install state=…`, with `chk= mis= misrd=
  cdmis= cd2rd=` behind it, and the desktop log says the same thing as
  `installer: NeoBench written to hda (4096 KB)`.
- **The ATAPI byte transfer count, at last set** (`boot/rom/ata.c`).
  `$FFFF` now goes into the cylinder registers immediately before
  `PACKET`, which is how a host asks for a whole sector in one piece.
  Left as the previous disk command found them, those registers held
  part of a disk address, so every 2048-byte CD sector came back in
  phases of a few bytes with a busy gap between them — the transfer,
  reading 1024 words blind, walked through every gap, which is where the
  corrupted install windows came from. The packet itself is sent as the
  twelve bytes it is instead of twenty: after the twelfth the device
  starts the transfer, and a word arriving behind it lands in the data
  instead of in the command.

### Documents

- **NeoText**, the reader, opens every file in the store: plain text as
  written, a PDF with its text lifted out of the page, and anything else — an
  executable, a sound, an archive — as a hex view of the bytes it is, eight to
  a line. A document chosen in Files opens in it, and the start menu starts a
  program; a program entry in a drawer starts its program instead of opening
  the reader.
- **PDF text extraction** (`boot/rom/pdf.c`): page streams are inflated by
  `boot/rom/inflate.c`, a raw DEFLATE decoder written for the purpose — no
  zlib header, no allocation, no recursion, every bounds check made — and the
  text operators are walked. `Tj`, `TJ`, `'` and `"` show their strings, `T*`
  and `Td`/`TD` and a `Tm` that moves down the page put the line breaks back,
  two moves in a row give the blank line between paragraphs, and kerning
  narrow enough to be a space becomes one. Image and embedded-font streams are
  skipped rather than guessed at; a document set through a font NeoBench
  cannot read falls back to the hex view instead of pretending.
- `system/Core/Docs/guide.pdf` ships as a real document to open: two pages,
  Flate content streams, a real cross-reference table, written for the reader
  to read.
- The reader is honest about the store: a text file can be typed into and says
  so on its status line — edited, not saved — because PFS has no write path
  yet.

### Preferences

- **Preferences is filed in `Config/`.** `Config/Preferences` is a program
  entry rather than a document — its first line reads `program = preferences`
  — so Files lists it beside the four files the boot reads, wearing the rose
  tile it has in the start menu with `run` where a document would show its
  size, and choosing it starts the pane instead of the reader. The entry is
  generic rather than a hard-coded name: any directory can carry one, and a
  file that says nothing of the kind still opens in NeoText, so a typo costs
  the shortcut and nothing else.
- **Preferences comes off the start menu.** The menu lists the other five
  programs and the panel ends with them; the pane is started from `Config/`
  instead and still takes its button on the task row, and still answers to
  Escape and to the panel's focus like every other program.
- **A Preferences pane** carrying the backdrop: wash, paper, azure, dusk and
  slate, chosen with one press and painted at once, with the hairline grid and
  the horizon glows as check boxes beside it. `backdrop = ...` in
  `Config/screen.cfg` says which one the machine comes up with; only the file
  says that, and the pane decides for the session.
- The four fixed pairs are five hues of one light field rather than four new
  desktops, so the mark, the wordmark, the glows and the Workbench chrome over
  them read the same on every one.

### Tools

- **`Tools/` is a drawer of program entries, and NeoText is the first one in
  it.** `Tools/NeoText` reads `program = neotext` on its first line, exactly
  as `Config/Preferences` reads its own, so the reader starts from a tools
  drawer as well as from the start menu or from the document that needs it.
  The drawer carries `tools.txt`, its own note saying what is filed there and
  what is coming, so a directory holding a program says so the way `Config/`
  does. NeoShell and the calculator are the next two names for it.
- **Files shows seven rows where it showed six.** The root carries six
  drawers now — Apps, Config, Core, Home, Temp and Tools — so the list, its
  object count and the window's height grew by one row.

### Desktop

- **Windows can be picked up and carried.** Press the caption — anywhere in
  the strip that is not one of its three buttons — and the window goes where
  the pointer goes until the button comes back up, taking the keyboard with
  it on the way, as a press inside a window always does. The clamp is on the
  caption rather than the frame, so a window may stand half off an edge, as
  windows do on any desktop, but enough of the strip stays on the screen to
  be picked up by again. All six move: the four program windows by their
  captions, the dial and the monitor by their own bodies.
- **The windows are drawn in Aero**, which is what the bar and the start menu
  were already drawn in: a pane of glass across the caption with the backdrop
  still reading through it, a light steel rim, a shadow thrown below and to
  the right, and Workbench's grey body and white field inside. The monitor's
  caption follows the same terms, so one corner of the screen does not carry
  two desktops at once.
- Two more programs in the start menu and six buttons on the task row, with
  the row dropping the buttons that no longer fit rather than running off the
  end of the bar.
- Escape now puts down whichever program has the keyboard — the cross does it
  for the pointer, and the keyboard has the same answer.
- Choosing a file in Files opens it in NeoText instead of stepping into the
  node, which is what it used to do whether the node was a directory or not.
- The draw and click passes order the windows by the keyboard: the one being
  used is painted last and asked first, so a window that covers another never
  passes a press down to the one it covers.

### Boot

- **The chainload returns instead of crashing.** `SYS:NeoBench` was ending as
  `Software Failure … Program failed (error #80000000)`, four defects deep.
  Two were on the serial line: the transmitter-ready test polled `$DFF038`,
  which is `strequ` and write-only, so it read a register that never changed
  and the whole boot message collapsed to a single byte; and the byte itself
  went out under `move.b`, which a 68040 or 68060 turns into a word store of
  the value shifted left eight, putting `$00` on the wire and nothing else.
  Both are fixed in `boot/rom/chain.S` and `boot/block/bootblock.S` —
  `$DFF018`, and `andi.w #$00ff` followed by a word store, which is the form
  `boot.S` and `amiga.c` were already using.
- **Entering in user mode is no longer a privilege violation.** `move.w %sr`
  is privileged from the 68010 up, and AmigaDOS starts a program with the
  supervisor bit clear, so it faulted on vector 8 before the first message
  could be printed. The arrival mode is now established without touching SR:
  vector 32 is swapped for a handler, `trap #0` is taken, and the handler
  compares the user stack pointer against the stack pointer saved before the
  trap — the same value means user mode. No exception frame is read, so `rte`
  returns whatever format the CPU built.
- **`RunCommand` is handed a return code it can read.** `d0` was left holding
  whatever the last routine put there, and `RunCommand` passes that on as the
  program's exit status; it is now `moveq #0, %d0` before the `rts`. With all
  four fixed the serial line reads `*` and one message, `NB-CHAIN: after
  neobench` follows it, and `LoadWB LEGACY` brings Workbench up.

### Devices

- **`scsi.device`: the NCR53C710 on the A4000T** (`boot/rom/scsi.c`). One AGA
  machine carries a SCSI host with no expansion at all, so the driver looks
  for the chip and not for the machine: CTEST1 has to answer `$F0` first, then
  a scratch register has to round trip `$5A`/`$A5`, and only after both is the
  chip initialised. The window is decoded the way the board wires it rather
  than the way the CPU reads it — register R sits at `$DD0040 + (R xor 3)`,
  mirrored at `$DD0080`, so CTEST1 lands on `$DD0056` and everything below
  `$DD0040` is left for Gayle. Both reads are harmless on an A1200, whose
  Gayle answers `$00` outside the IDE registers and ignores writes there, so
  the bus is found empty the same way as anything else: by its silence,
  reported as `[ WARN ] scsi.device: no NCR53C710 host in the Gayle window`.
- **Every command is a SCRIPTS program in `.bss`.** The chip fetches its
  instructions by address while this ROM is not writable, so a program is
  built per command: select with ATN, identify, the command, the data phase
  that command names, status, message in, and the transfer control
  `$98080000` that stops the chip and raises the interrupt. INQUIRY and READ
  CAPACITY run during the probe, which is where
  `[  OK  ] scsi.device: UAE     install.hdf, 64 MB (target 0)` comes from,
  and sector traffic then goes through the same program with the data phase
  aimed at the caller's buffer. A refusal names the phase it stopped in, the
  status byte, DSTAT, SSTAT0/2 and the address of the instruction that was
  running — enough to place a fault without a logic analyser.

### Tests

- `tools/tests/test_scsi` holds the parts of `boot/rom/scsi.c` that are
  arithmetic against the values the emulator decodes them with: the register
  decode and its `$DD0056` anchor, the SCRIPTS encodings for a block move, a
  select and the terminator, the phase numbers, and the READ(10)/WRITE(10)
  descriptor layout. It caught the encoding lying while it was being written —
  ATN was baked into the base word of `SCN_SELECT`, leaving the argument free
  to make no difference at all — and it exists mainly because a length written
  one byte too far is a legal command asking for nothing, which completes
  without moving any data, so nothing downstream ever notices it was wrong.
- `tools/tests/test_iso9660` runs the whole install disc on the host: the
  disc itself is pressed by the same `mknbfs.py`/`mkiso.py` that press the
  real one, and behind it two fake devices registered in the device table
  under their real names — `atapi.device` reading the ISO image,
  `ata.device` a RAM disk — so the reader and the installer see exactly
  what they see on the machine. Mount, volume identity, root listing, path
  lookup either way and file bytes across a sector boundary; install
  refused when there is no disk, install onto a blank disk and the bytes
  it leaves behind, the same volume recognised on the next boot, a disk
  holding someone else's data refused, and a damaged volume declined.
- `tools/tests/test_pdf` drives both new readers from the host: stored, fixed
  and dynamic blocks against data zlib produced, every error path, and a
  four-object PDF — a compressed page, an image that must be skipped, an
  unfiltered page and a stream that is not text — with the expected extraction
  written out in full.

## 0.1.6

### Boot

- Boots by the genuine chainload path: an AmigaOS 3.2.3 ROM boots first and
  starts NeoBench as a Hunk executable from `S:Startup-Sequence`, built with
  `make -C boot/rom chain`. The same image still runs directly as a Kickstart
  ROM replacement, which is how it is exercised in an emulator.
- Fast RAM is a requirement rather than a preference: `[FAILED]` below 128 MB,
  `[ WARN ]` below the 136 MB preferred, green from there up. Both figures are
  constants in `boot/rom/probe.h`.
- The desktop keeps the NeoBench backdrop under Workbench-3.2-style chrome,
  with the startbar at half height, icons at a 24 px half-size tile, a palette
  of 128 colours, and an Aero-style bar.
- Play-once boot chime and the NeoBench store.

### Devices

- NeoBench's own `.device` layer (`boot/rom/dev.c`): named devices with units,
  sector sizes and read/write entry points. A device is in the table only
  because the hardware it names answered, a driver with no sector path reports
  `io=none` rather than pretending to be a disk, and an ATAPI medium reports no
  write path at all.
- `ata.device` — PIO mode 0 over Gayle, LBA28 reads and writes, moved a byte at
  a time so a buffer at an odd address is legal.
- `atapi.device` — ATAPI READ(10) in 2048-byte blocks, bound to a unit that
  announces itself as a packet device in the cylinder registers (`$14/$EB`).
- `sdcard.device` on the same bus, `sound.device` on Paula, `zz9000.device`
  and `zz9000ax.audio` only when that card is found.
- A boot-time read self-test: sector 0 of the first disk and the volume header
  at LBA 16 of a CD are read through the device table and reported. Writes are
  not tested at boot — a wrong byte order there would corrupt the boot disk —
  so the write path was proved once against a scratch file instead.
- Three faults found by measuring rather than assuming: a unit must be
  selected before its status means anything, `$00` is not an empty bus (it is
  also a drive that has not come ready), and IDENTIFY must be decoded in the
  byte order word 0 pins down rather than by scoring the model name, which
  cannot tell the two readings apart.

### Zorro

- ZZ9000 and ZZ9000AX detection by a read-only walk of the autoconfig space at
  `$E80000` and `$FF000000`, one 64-byte slot at a time: structure validated
  before a slot is believed, manufacturer `$6D6E` and products 3/4/5 matched
  exactly before a single byte is written, and no shutup write.
- Host unit test `tools/tests/test_zorro` over recorded slot images, since
  FS-UAE drains the autoconfig chain at reset and so never shows a card.

### Input

- Three backends into one queue of 64 keys (`boot/rom/kbd.c`): the Amiga
  keyboard on CIA-A, the serial port on Paula, and a USB HID boot-protocol
  parser. Each answers for itself, and every key that arrives is logged by
  source, raw code and what it decoded to, so a missing key says which of the
  three never sent it.
- The handshake the hardware asks for: a code is read from the SDR, split into
  code and break (bit 0), then acknowledged by pulsing SPMODE — `$00` in the
  SDR first, the mode bit held for a bounded spin, then cleared with CRA's
  other bits read back so Timer A is not disturbed. One code is outstanding
  at a time, which is what the protocol's mode 0 requires, and CIA-A's serial
  interrupt is polled through the ICR, which returns the flags and clears
  them — a level to check, not an edge to count.
- `input.device` and `serial.device` are registered always, like
  `sound.device`, and are not counted as bound: they are not disks, and the
  counters count disks. The new boot line reports the keyboard, and
  `usb.device`'s warning is built from an actual Zorro walk rather than a
  constant.
- The keymap is the ROM's USA0 default plus the British national keys — `#`
  and `~` on the key beside Return, `\` and `|` beside Left Shift. Caps Lock
  flips letters only; the keypad parens have no Amiga key and answer nothing.
- The HID parser has no hardware dependency at all, which is what makes it
  testable: `tools/tests/test_hid` runs it on the host. On AGA it is
  unreachable by construction — AGA has no PCI bus — and `nb_kbd_usb()`, the
  entry a host controller would call, is documented as never called there.
- The desktop takes keys: the Amiga keys open the start menu as the orb does,
  Escape dismisses the menu before it dismisses a selection, Up and Down walk
  the standing list, and Return activates what is lit through the same call a
  double press makes — one path to an activation, nothing to keep in step.
- The main loop polls the input stack while it waits for the field, so keys
  are read a pass at a time instead of only between frames.

### Documentation

- README: the device layer, the ZZ9000 probe, the input stack, and the
  caveats that belong to all three — what is proven, and what is merely
  implemented.
