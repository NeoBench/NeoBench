# Changelog

Notable changes to NeoBench, newest first. British English throughout.

## 0.1.9

### Rust

- **The first Rust object in the ROM, held to a selftest** (`libs/nb_rs`,
  `boot/rom/Makefile`, `kernel/init/kernel_main.c`). `nb_rs` is a static
  library cargo builds for `m68k-unknown-none-elf`, the tier-3 bare-metal
  target, `core` for it compiled from `rust-src` by the stable toolchain
  with `RUSTC_BOOTSTRAP=1`; the ROM link takes the archive beside the C
  objects it has always taken. Two pins keep it honest with what is
  around it: `RUSTFLAGS` fixes LLVM's CPU at `M68000`, because the
  target's own default is an `M68010` and everything else here is 68000
  code, and the archive is linked *after* the objects so `lib32.c`'s
  `__mulsi3` and `__divsi3` answer first, leaving `compiler_builtins`
  unopened. `nb_rs_selftest()` runs at boot over six identities — division
  with its remainder, signed and unsigned, multiplication against
  thirty-one additions, rotation against shift-or, byte order in memory,
  and a loop both halves can count — each against a value the compiler
  did not know when it compiled the code, so the test cannot be folded
  into a constant that always passes. It reports on serial alone
  (`>rs ok`), because a selftest that passed has no place in the log; only
  a failure earns the amber line `Rust codegen selftest`. 222 bytes of
  ROM text, `_end` unmoved at `$00139584`, and both ways in verified: the
  ROM boots at 11 WARNs / 0 FAILED, and the chainload from a genuine
  AmigaOS 3.2.3 `S:Startup-Sequence` answers `NBCHAIN mode=U` and
  `NBCHAIN cacr=00000000` before the same `>rs ok`.

### Installation

- **An install disc, pressed by `make iso`**: `images/NeoBench-0.1.9.iso`,
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
- **`system/Core/Docs/neoshell.txt`, the NeoCommands reference.** The same
  thirteen commands `help` prints at the prompt, written out with what each
  of them reads and what none of them can do, the keys the line takes, the
  three ways in — the hold, `failsafe`, and the entry in Tools — and the one
  way out. It is a document like any other: Files opens it in the reader,
  and `type neoshell.txt` from the shell prints its first thirty lines.
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
  does. NeoShell stands in it now; the calculator is the next name for it.
- **Files shows seven rows where it showed six.** The root carries six
  drawers now — Apps, Config, Core, Home, Temp and Tools — so the list, its
  object count and the window's height grew by one row.
- **NeoShell is the second entry in it.** `Tools/NeoShell` reads
  `program = neoshell` on its first line, exactly as its neighbours do, so
  the command line starts from the drawer the way the dial and the reader
  do. The browser draws its tile for it in the sky hue the set keeps for
  it — a screen cut out of the ramp, a chevron standing on its first line
  and a caret under that, which is the one mark none of the others
  carries — and the drawer's note says what it is and what boots with it.
  Its name comes out of `prog_name[]`, hoisted out of `program_slot_of()`
  and shared with the task row and with the shell's own `progs`, so the
  store's entries and the command line read one list rather than two that
  have to be kept in step.

### Media

- **VLC, a player for what the store holds — the seventh program.**
  `N_PROGRAMS` is eight now, the media player being the seventh of them: it
  takes slot 6 (`focus_p` 7, `win_open` case 7),
  a button on the task row, a stop in Tab's round and
  an entry of its own in the store at `system/Core/Media/VLC`, so Files
  starts it the way it starts any program and the browser draws its icon
  for it. It is the only one of the eight that has to look at the store
  before it can show anything at all: `vlc_scan()` walks four drawers —
  `Home/Pictures`, `Home/Videos`, `Home/Music` and `Core/Media` — and
  keeps the files it can read, to `VLC_MAX` (24) of them. The window is a
  240×136 pane with the list and the name beside it and the transport
  under that: `window_vlc()` draws the whole of it, `hit_vlc()` takes the
  presses in it, and `vlc_key()` takes the keys — Space or Return starts
  and stops, the left and right arrows walk a film a frame at a time, and
  Escape puts the player down like anything else.
- **Six decoders, and what each of them will and will not read.**
  `vlc_kind_of()` decides from the first bytes of a file which one to
  call, and a file that fits none of them is reported as unsupported
  rather than guessed at: portable pixmaps, P6 and P3, comments and all,
  maximum value 255; Windows bitmaps of 8, 24 or 32 bits, either way up,
  uncompressed, with the palette an eight-bit one names; network graphics
  of colour types 0, 2, 3, 4 and 6 at eight bits a sample, not
  interlaced, one run of image data or several, and all five of the row
  filters; IFF ILBM of one to eight planes, uncompressed or ByteRun1
  over the whole body at once; YUV4MPEG2, four-two-zero, at whatever
  frame rate the header carries and nothing else — a file that names
  another sampling is refused rather than decoded as though it had named
  none; and the two sounds, RIFF wave of eight or sixteen bits in one
  channel or two, and NeoBench's own NSND. Both chunk-bearing formats
  read their chunks the way they are written: the name first and the
  length after it, which is what IFF and RIFF do and what a PNG does
  not — a PNG's length comes before its name.
- **Where each decode goes.** A picture is built in `vlc_pix`, 65,280
  bytes of `.bss` and the one new thing in it that is large, and handed
  across to the pane when it is finished; sound goes to the chip Paula
  reads at `NB_SND_BASE`, which the player has stopped before it starts.
  A PNG inflates into that same chip buffer, because the raw bytes behind
  a picture are more than the pane can hold compressed, and when a file
  carries its stream in several runs they are gathered into `vlc_pix`
  first, since that is the only room big enough for them. Every size is
  checked against the pane before the decode begins, so a picture too big
  for it says so rather than running off the end of the frame, and
  nothing any of this does comes near `$D80000..$FFFFFF`.
- **The film steps on the field count, not on a press**
  (`nb_desktop_tick()`). Every other program here answers a press and
  costs nothing between them; the player has to move by itself, so the
  kernel's input loop asks it once a field, after `nb_sound_poll()`,
  whether anything is due. It repaints through `band_vlc()` and
  `nb_desktop_render()` only when a frame actually is, and otherwise
  returns without touching the screen. Hiding the desktop does not stop
  what is playing: the window goes down and the sound does not, because a
  player that had opinions about what you were doing would be a poor one.
- **Six samples, and the tool that makes them** (`tools/genmedia.py`).
  The tree ships one file of each kind it reads — `Home/Pictures/` gets
  `spectrum.ppm`, `tiles.bmp`, `plasma.png` and `rings.iff`,
  `Home/Videos/` gets `orbit.y4m` and `Home/Music/` gets
  `arpeggio.wav` — 49,082 bytes between them, all of it written from
  arithmetic that does not depend on the day it is run, so regenerating
  changes nothing in the tree unless a shape in the script changes. The
  colours are deliberately smooth because the desktop quantises every
  pixel to one hundred and twenty-eight of them anyway, and a sample of
  near-identical colours would be a sample of the quantiser rather than
  of itself. Each format is exercised where it is least likely to be
  trivial: the network graphic cycles all five row filters and carries
  its stream in two runs of image data, and the sound is sixteen bits in
  two channels so the downmix has a pair of channels to downmix.

### Desktop

- **NeoShell, the command line — the eighth program.**
  `N_PROGRAMS` is eight now: NeoShell takes slot 7 (`focus_p` 8, `win_open`
  case 8), a button on the task row, a stop in Tab's round, a row of the
  z order and an entry of its own in the store at `system/Tools/NeoShell`,
  so Files starts it the way it starts any other program and the browser
  draws its terminal tile for it. The window is a 536×360 pane of the wash
  the wallpaper carries, lit along its top edge with the prompt standing at
  the foot: `window_shell()` draws it, `hit_shell()` takes the presses in it
  and `sh_key()` takes the keys — left and right carry the caret, backspace
  takes the letter in front of it, Return runs the line, and the two arrows
  bring back what has scrolled off the top of the pane, which is the one
  thing this shell does with them and the reason they are not handed to a
  list standing behind it. Escape still puts the program down like anything
  else; on the boot screen, where there is no window to put down, it clears
  the line.
  What it holds is a ring of forty-eight lines and the line being typed, and
  the commands — NeoCommands — read only the store the ROM carries and what
  the hardware probe answered: `help`, `ver`, `cls`, `echo`, `info`, `cfg`,
  `pwd`, `dir`, `cd`, `type`, `progs`, `run` and `desktop`. A verb is read
  without regard to case and a name after one is spelled as the store spells
  it, also without regard to case; there is no path outside the store for a
  command to name, which is what makes this a shell for NeoBench rather than
  a shell that happens to be running on it. `Core/Docs/neoshell.txt` carries
  the same list `help` prints, and the `>ui` dump on the wire carries `h=`
  for whether the program is up, with the rest of the flags.
- **Wash, Vista: the Aurora under the Aero chrome**
  (`system/Config/screen.cfg`, `user/gui/desktop/main.c`). `bg_top` and
  `bg_bot` now run from a deep navy `#0A1E45` down to `#0A4E86`, the blue the
  glass is cut for — the two colours a store without a `screen.cfg` falls
  back to as well — and the wash is Windows Vista's own wallpaper: the
  aurora, five blooms laid end to end down the screen from the upper left to
  the lower right, overlapping by more than half their own radius so the run
  reads as one ribbon of light rather than five lamps, white at the two
  points where its core shows. It crosses the mark, which is what lights the
  branding rather than inks it, and has fallen clear of the wordmark by the
  time it reaches that. The artwork reads the weight of the field it stands
  on as before: `wallpaper()` asks the top colour whether it is a night one
  and answers with the aurora's three blues and a pale hairline grid that a
  cream field never sees; the four fixed backdrops are untouched and still
  light, where the ink does the work exactly as it did. The start orb is
  Vista's on the glass too — a lit blue sphere, its top turned to the sky the
  glass is cut for and its foot falling to a navy — with NeoBench's mark
  still on the pearl and the brand teal still thrown as the halo when the
  menu opens, which is the one piece of that desktop's branding that is not
  somebody else's. The caption is
  glass rather than a tint now — brightest under its own lit edge, stepping
  down, shading where it meets the body, three alphas — the title is set
  white over a pixel of the pane's own dark, and the close button is Vista's:
  red, lit along its top edge, pooling dark at the foot, the cross cut in
  white, and the only warm colour on a window, which is the whole of what
  makes it the one the eye finds without reading.
- **Tab walks the keyboard round the windows, and Ctrl with a cursor key
  carries one.** Tab gives the keyboard to the next program that is running,
  in the order the task row numbers them and wrapping at the end; Ctrl with a
  cursor key moves the window the keyboard is in by eight pixels, clamped to
  the same wall as the drag. The start menu keeps both while it stands, as it
  keeps the cursor keys, and the reader's page moved off Tab onto Shift with a
  cursor key, which is the key the keymap has no Page Up or Page Down for.
- **A task button raises as well as puts down.** One press on the button of
  the window that is in front still puts it down; one press on any other
  button raises its window and gives it the keyboard, which is what the lit
  button claims to be showing. A change of keyboard repaints every window
  that is up, because the keyboard and the z order are the same question
  here -- the window with the keyboard is the one drawn last, and it is not
  only where two windows overlap that the picture changes.
- **Windows can be picked up and carried.** Press the caption — anywhere in
  the strip that is not one of its three buttons — and the window goes where
  the pointer goes until the button comes back up, taking the keyboard with
  it on the way, as a press inside a window always does. The clamp is on the
  caption rather than the frame, so a window may stand half off an edge, as
  windows do on any desktop, but enough of the strip stays on the screen to
  be picked up by again. All eight move: the six program windows by
  their captions, the dial and the monitor by their own bodies.
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
- **The start menu leads with the drawers, and the programs are filed where
  they belong.** Files moves up out of the program list and becomes the first
  drawer, opening the browser on the root of the store — the one place none
  of the other five named — wearing a two-drawer cabinet mark of its own
  rather than the folder Docs carries. Being a place rather than a program it
  opens instead of toggling, and the cross is what puts it away; its button
  on the task row is the same as it was. Clock and Monitor become entries in
  `Tools/`, beside NeoText and the note that says what the drawer holds, and
  About becomes one in `Core/Docs/`, beside the documentation it summarises.
  Clock, Monitor and NeoText then come off the menu altogether, and About
  follows them in the same pass: a program filed in a drawer is started from
  the drawer, and `Apps/`, `Core/Docs/` and `Tools/` all say so.
- **The start menu sets its name down its left edge.** The caption comes off
  the rule at the head of the pane and turns a quarter, standing in a band
  down the left of it with a divider beside the band, so it reads down the
  strip instead of across the top and the rows take the width the header used
  to hold. Under Aero the band is a step of the pane's own light, and the
  chosen row stops being a flat block of Workbench blue: it lights like every
  other glass surface here, a teal rim round a field running from its lit end
  at the top to its dark end at the foot. The flat terms are untouched —
  `bar = classic` still gives the grey panel, the blue row and the hard edge.
- **The menu is set in alphabetical order, and stands in the bar.** Both
  sections sort: the drawers run Bench, Core, Docs, Files, Home, Media, and
  the rows under the rule run **Preferences** then **VLC** — Preferences
  comes back to the menu and still stands in `Config/` beside the files it
  reads, VLC is the player described above, and About is the one that goes,
  since a program filed in a drawer is started from the drawer. A menu is a
  list the eye runs down looking for one name, and no entry is worth
  keeping at the head of it against that.
  The panel's foot is the bar's top edge now, so it rises out of the
  furniture the orb sits in rather than floating over the middle of the
  screen, and it throws no shadow down there — nothing comes between the
  two.
- **The menu is flush with the screen's edge and answers a letter.**
  `MENU_X` is 0, so the pane stands against the left edge of the display
  rather than a little way in from it. It has no left margin to carry:
  the band that sets its name down is already the thing at its left, and
  a pane hanging in the middle of the screen with a band beside it would
  be a pane that had missed. Type a letter while it stands and the choice
  jumps to the next entry beginning with it, in either section, wrapping
  past the one already chosen (`menu_jump()`); an uppercase letter and
  its lowercase are the same key, and a letter nothing is called by
  leaves the choice where it was rather than clearing it.
- **The present is staged, so a repaint no longer blanks the screen**
  (`gfx_present()`). The palette goes up under a hold of its own — a
  hundred-odd register writes, a fraction of a line, and not something
  that can be let out halfway — and the pack then runs with the
  fetchers going. It used to run held as well, which put slot 0 of the
  palette on the screen for every row of it: the right answer while a
  frame could be packed inside a field, and a machine that went blank
  and came back once a frame took seconds, which is what opening a
  program did. Left running, the rows the pack has not reached still
  hold their old numbers, and old numbers read through a new palette
  are the old picture in the new colours — the menu, the bar and the
  text all still where they were — while the new picture comes up
  behind them from the top down, in the order the raster reads it. A
  half-updated picture rather than no picture at all. The bill is bus
  time: with the fetchers asking for chip RAM while the frame is
  rewritten, a repaint takes about twice as long as it did behind the
  hold, which is a slower one you can watch instead of a fast one you
  could not see.

### Fonts

- **The same Xen, a size bigger** (`boot/rom/fonts/xen11`,
  `build_font_amiga.py`). `font =` in `Config/screen.cfg` takes a third
  choice, `Xen11`: the standard face cut at eleven rows from the Amiga font
  of that name, same one pixel strokes and the same eight pixel cell — Xen11
  advances seven on every character, so `strw()` counts it exactly as it
  counts Xen and nothing the chrome is built round has to move. The cutter
  no longer sizes the face itself: the y size comes out of the font, and so
  does the first character it draws, which is the one place the two sizes
  disagree — nine starts at 32, eleven at 0, and reading them before led to
  a face whose space was a blob and whose `A` was an exclamation mark. The
  header is named after the file it is cut from (`NB_XEN11_H`, `fontxen11`,
  `NBFONTXEN11_H`), which is what lets both faces be in one program without
  either name meaning two things, and what they hold in common — the
  32..126 grid and the hollow block behind it — is written once behind an
  `ifndef`. The nine pixel header regenerates byte for byte what it was,
  every one of its 96 glyph rows the proof that the cut did not move.
- **A line steps by the face it is set in** (`gfx_font_pitch()`). `gfx_text()`
  used to add nine for a newline whatever the face was, and the reader laid
  its rows out at nine itself, which is the pair of them disagreeing the
  moment a preference changed: an eleven row face would have come back
  through the line it had just set, and twenty-two rows of it would have run
  out of the pane. The line now asks the face — never less than the nine
  both existing faces have always stepped by, never less than a face's own
  rows — and the reader divides the pane it is given by the same number, so
  `Xen11` gives it eighteen rows in the same 198 pixels rather than
  twenty-two of them crammed. Nothing else in the desktop counts lines: a
  sweep for arithmetic on nine finds only icon drawing and the window
  nudge.  The tray's uptime readout is the one place that had pinned
  its two lines rather than measuring them: 493 and 503 are the nine
  pixel face's own fit inside a twenty-two pixel bar, the label ending
  on the screen's last row, and in eleven they would have come back
  through each other and run two pixels off the bottom.  It is set from
  the bottom of the bar up now, which lands on 493 and 503 exactly as
  they were and keeps the pair inside the bar whatever the face is.
- **Xen is cut from the Amiga font of that name** rather than drawn by hand.
  The face NeoBench sets type in everywhere now comes out of `fonts/xen9`
  under `boot/rom/`, read by `build_font_amiga.py`: an Amiga disk font is a
  hunk file, one `HUNK_CODE` carrying a glyph strip — every character of the
  face laid side by side, one bit to the byte — and a table after it giving
  each character's position and advance. The script walks both and re-cuts
  the face into NeoBench's own grid: nine rows, eight pixels of advance,
  which is the bargain `strw()` and `gfx_text()` already strike, each glyph
  centred in its cell so column 7 stays clear and no two characters can
  touch. Nothing the layout leans on moved — capitals still sit in rows 0
  to 6, the x-height in 2 to 6 and the descenders in 7 and 8 — so the type
  on the chrome is where it was, only drawn as Xen actually is.

### Boot

- **Three seconds to decide where the machine boots, and NeoShell is what
  they can be spent on** (`kernel/init/kernel_main.c`, `boot/rom/prefs.c`).
  The hold after the chime is a window as well as a pause: the receivers are
  polled through it for the whole of it and Escape calls NeoShell up instead
  of the compositor, so a machine that will not start its desktop — or that
  is being asked about rather than used — is reached with no tool and no
  rebuild, and a key typed early is still in the queue when the hold begins.
  The grey hint line over it says so, `Config/boot.cfg`'s `failsafe = on`
  asks for the shell outright for a machine kept for recovery, and a store
  with no `Config/screen.cfg` in it goes there with a warning: the wash, the
  bar and the face all come out of that file, so there is no desktop to
  composite. All three print `[ OK ] Reached target NeoShell.` and land at
  the same prompt, and the shell is the thing that decides when the desktop
  comes up — `desktop` at the prompt is what returns here — which is what
  makes it a place to fall back to rather than one to be stuck in. `failsafe`
  is read, defaulted to off and dumped with the rest of `boot.cfg`.
- **The BFG9060 says what it is** (`boot/rom/probe.c`). The A3000/A4000
  CPU-slot accelerator is not on the Zorro bus, so no scan of `$E80000` or
  `$FF000000` can ever mention it: the entry that names it is added to
  expansion.library by a resident in the card's own flash, which is software
  NeoBench does not run. What the card does leave is the word its own
  bootrom goes looking for — `$BF690600` with the firmware version in the low
  nibble, at `$FF040000`, sixty-four longs deep — and that is what the probe
  reads now: the Zorro III configuration space, whose rule is that it is read
  and never written, and only on a bus that carries all 32 bits, because on a
  24-bit machine the address truncates to `$040000` and the scan would read
  chip RAM and call it a signature. A machine without the card prints
  nothing: an A1200 cannot have one, and a signature that does not answer
  says nothing about which machine this is, so a colour for its absence would
  be a rumour. The miss goes on the serial line as `>probe … bfg=none`
  beside the rest of the probe's internals, and a card that answers puts
  `BFG9060 detected (firmware n)` in the log. `tools/tests/test_bfg.c` holds
  the decoder against the word itself, since neither the emulator nor the
  build machine has the card to ask: open bus either way round, the memory
  probe's two patterns, its megabyte marker, the signature shifted and one
  bit set wrong all answer nothing.
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
- **The chainload takes the machine over.** `chain_entry` in
  `boot/rom/chain.S` reported where the hunk had landed and returned to
  AmigaOS, which is all the diagnostic it had been built as. It now finishes
  the job: the arrival mode and the load address go out as `NBCHAIN mode=U
  (user mode) at=4005B170`, `chain_safe` decides whether the image may stay
  where the loader put it — one megabyte end to end, ending before the two
  longs the probe claims at the top of every megabyte, and clear of the chip
  ranges the frame buffer, the sound buffer and the stack own — and a
  refusal still goes back with `NBCHAIN image in the way, returning to
  AmigaOS` and a prompt. Supervisor mode is reached without a privileged
  instruction: a `trap #0` to a handler that never returns when AmigaDOS
  started the program in user mode, and a call when it did not. What takes
  over masks everything, puts the cache control register on the wire, drops
  the boot overlay, copies the vector table out of the hunk to address 0,
  takes the reset stack at `$001FFFE0`, zeroes `.bss` and enters `rom_main`
  exactly as a reset boot does — so the genuine ROM boots first and NeoBench
  still ends up owning the chipset, the vectors and the display.
- **The chainload hunk carried link-time addresses, and LoadSeg turned them
  into the wrong ones.** A `HUNK_RELOC32` word is loaded with the address
  the hunk landed at added to it, so what the image has to hold is each
  reference's offset from the link base: `tools/elf2hunk.py` subtracts that
  base before emitting now, and `ram.ld` says so. Nothing had noticed
  because the chain entry was PC-relative throughout and returned before
  calling any C code — from `rom_main` on, every global, string and vector
  landed `$01000000` too high.
- **The copper is stopped at last** (`boot/rom/amiga.c`). `DMAF_COPPER` was
  written as `$0002`, which is `AUD1EN`, so COPEN was never cleared at all:
  harmless on a reset boot, where nothing has enabled it, but under a
  chainload AmigaOS's copper list was still in control and reloaded AmigaOS's
  bitplane pointers every frame, immediately after the vertical blank had put
  NeoBench's own in. The desktop rendered, `>present` and the pointer both
  reported it, and the screen stayed on Workbench. The NDK's
  `hardware/dmabits.i` says `$0080`, and the code says that too.
- What the OS leaves behind in the cache control register is reported rather
  than changed: `NBCHAIN cacr=00000000` on the wire, which is the answer
  that says no flush is owed. The chainload path was exercised against
  AmigaOS 3.2.0, the only ROM available here; the documents describe 3.2.3.

### Devices

- **An empty IDE bus stops reading as a ready one** (`boot/rom/ata.c`).
  `floating()` knew `$FF` and nothing else, but a channel with nobody on the
  other end reads `$7F` — every line but BSY — which carries DRQ set in it,
  so `wait_drq()` reported data ready where no device was standing and the
  IDENTIFY spent 256 word reads on a transfer that was never going to come.
  Twice a boot, once a unit, and the emulator logged one line for every read:
  491 of `IDE1 DATA but no data left!?` in a run. `$7F` cannot be a device's
  own status, since bits 1 and 2 of the status have been reserved since
  ATA-1 and it needs them both, so it is floating now — and the signature is
  seeded with the `$FFFF` an unanswered bus reads, because a unit nobody
  answers selection for no longer reaches the cylinder registers at all.
  The boot log's `>ide u=0 st=$7f sig=$ffff id=$ffff` is unchanged for it,
  and the refusal it reports is the same refusal; only the 512 reads that
  proved nothing are gone.
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
