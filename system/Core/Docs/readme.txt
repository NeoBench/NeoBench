NEOBENCH 0.1.7 -- m68k / AGA
============================

NeoBench is an operating system for 68060 AGA and RTG Amigas
(A1200/T and A4000/T).  The AmigaOS 3.2.3 ROM boots first and hands
the machine over; after that NeoBench runs on its own.  It does not
mount AmigaDOS volumes and it does not call into AmigaDOS libraries.

Where things live
-----------------

The store is one tree with six places at the top of it, and none of
them is borrowed from anywhere else.  Open Files and walk down from
the root to see it.

  Apps/                  programs; the entries so far are filed in
                         Config/ and Tools/, and this drawer is kept
                         for the ones that arrive as files of their own
  Config/                settings, read at boot straight from the image
    Preferences          the pane that changes the desktop now; this is
                         a program entry rather than a document, so
                         choosing it starts the pane instead of
                         opening it in NeoText
    boot.cfg             how long the boot is held, and whether the
                         hardware is walked before drivers are bound
    pointer.cfg          mouse pointer: shape, scale, speed, colour
    screen.cfg           wallpaper, grid, glows, which of the five
                         washes paints it, taskbar, bar style, how much
                         of the backdrop the bar shows, face
    sound.cfg            the startup chime and its level
  Core/                  the operating system itself
    Bench/               the benchmark harness
    Docs/                this directory
    Media/               the startup sound and the desktop's artwork
  Home/                  your own content
    Desktop/             what the desktop would hold
    Documents/           writing
    Music/               audio
    Pictures/            still images
    Videos/              moving pictures
  Temp/                  where downloads land; there is no RAM disk
  Tools/                 the programs that are filed rather than
                         started from a document
    Clock                the dial: the time, and the machine's own
                         idea of it
    Monitor              the two-bar system monitor: what the machine
                         is doing with itself while you watch it
    NeoText              the reader, as an entry that reads
                         "program = neotext"
    tools.txt            what is filed in this drawer, and what is
                         coming

All four configuration files are read at boot, straight out of the ROM
image.  There is no configuration step: edit the file, rebuild the
ROM, reboot.

Preferences is the fifth entry in Config/: a program filed beside the
files it changes, so the pane that picks a backdrop for this session is
standing next to the file that picks one for the boot.  It is a program
entry rather than a document -- its first line reads "program =
preferences" -- and any directory can carry one; a file that says
nothing of the kind opens in NeoText as it always has.

The boot
--------

  1. the genuine AmigaOS 3.2.3 ROM runs first and hands over
  2. Config/ is read, before anything is drawn
  3. the hardware is walked and a driver is started for every device
     that answers -- fixed disks, CD-ROM drives and removable cards
     over Gayle IDE; a card that identifies itself as removable is a
     card reader rather than a hard disk
  4. the startup chime sounds, once: the pass is timed against the
     sample rather than left to Paula, which would play it again
  5. the log is held on screen for three seconds
  6. the desktop takes the screen

Steps 3 and 5 are preferences -- "scan" and "hold" in boot.cfg.

The desktop
-----------

The bar along the bottom is cut the way Aero cuts the Windows 7
taskbar: a pane of tinted glass laid over the backdrop with a lit edge
along the top of it, carrying the start orb, the pinned launchers, one
button per program that is running (the lit one is the window you are
in), the tray, the clock set in two lines, and a sliver on the very
edge that takes the desktop away.  "bar" in screen.cfg cuts the same
furniture the flat Workbench way it was drawn with first, and "glass"
says how much of the backdrop the pane shows through.

There are no icons on the wallpaper at all.  The five places the
desktop used to carry down its left edge -- Home, Core, Bench, Docs
and Media -- are entries at the top of the start menu the orb opens,
and the programs are entries under a rule beneath them.

The icons themselves are drawn the way MUI drew its own: a rounded
tile whose body ramps from a saturated tint to a deep one, a white rim
round it, an arc of light along the top of it, and the mark cut out of
the ramp in white.  One routine draws the tile at every size it is
used at -- 24 pixels in the menu, 16 on the bar, 14 for the programs,
12 in a directory row -- so the set stays one set as it gets smaller.
Five hues carry the places, a sixth of slate carries an ordinary file,
and no icon wears two colours for the same thing.

One press names an entry; the second press, inside half a second and
twenty-four pixels, opens it.  The orb, the window crosses, the task
buttons and the sliver are controls and answer to one press, and the
right button opens the menu in the sticky form that stays up across
several choices.

The programs
------------

The start menu lists two of them: Files, which leads the drawers
above the rule, and About, the only row under it.  The other four are
filed where they belong instead -- Clock, Monitor and NeoText in
Tools/, Preferences in Config/ beside the files it changes -- so each
is started from the drawer it stands in.  Tools/ is where NeoShell
and the calculator will stand when they are written.

  Files        the directory browser, reading the store the image was
               built with; choosing a file in it opens NeoText, and
               choosing a program entry starts that program
  Clock        an analogue dial, from Tools/Clock
  Monitor      two bars: fast memory in use, and the uptime, from
               Tools/Monitor
  About        what this build is
  NeoText      the reader, from Tools/NeoText
  Preferences  the backdrop, and the grid and the glow over it, from
               Config/Preferences

Preferences offers five backdrops -- wash, paper, azure, dusk and
slate -- and paints the one you choose at once; "backdrop" in
Config/screen.cfg says which the machine comes up with.  The grid and
the glow are check boxes in the same pane.  All five are the same
light field in five hues, so the mark, the wordmark and the chrome
over them read alike on every one.

NeoText opens any file: plain text as it is written, a PDF with its
text lifted out of the page, and anything else as the bytes it is,
eight to a line.  It follows the caret as you type and says on its
status line what it is showing and where you are in it.  The store
has no write path yet, so a file you have typed into is labelled
edited but not saved.

The windows are drawn in Aero terms -- a pane of glass across the
caption with the backdrop still reading through it, a light steel rim
round the frame, and a shadow thrown below and to the right -- over
Workbench's grey body and white field.  Press the caption anywhere
that is not one of its three buttons and the window goes where the
pointer goes until the button comes back up, taking the keyboard with
it on the way.  The clamp keeps the caption on the screen rather than
the frame, so a window may stand half off an edge and still be picked
up by again.  All six move: the four program windows by their
captions, the dial and the monitor by their own bodies.

The keyboard follows the window you are in: the cursor keys walk the
list that is standing or move the caret, Return opens what is lit or
starts a new line, and Escape puts down whichever program has the
keyboard.

Installing
----------

Two routes, and the machine can take either.

The install disc is pressed by

  make iso

into images/NeoBench-0.1.7.iso -- the disc this file came on, if you
are reading it from one.  It carries NBFS.IMG as its payload, the hunk
executable and the ROM beside it, and NeoBench's NBISO block in sector
0.  Attach it to the IDE bus beside a blank disk and boot: the ATAPI
driver reads sector 0, finds NBISO, mounts the ISO 9660 volume with
NeoBench's own reader and streams NBFS.IMG onto the disk.  Only a
blank disk is ever written -- a disk that already holds NeoBench
reports the volume by name, a damaged one reports itself damaged, and
anything else is refused with nothing touched.  INSTALL.TXT in this
directory is the step-by-step.  The disc does not boot the machine
itself; a desktop Amiga boots from hard disks and floppies, so the ROM
always starts first.

The other route is the chainload, which needs no disc after the copy:
copy NEOBENCH.EXE from the disc to the hard disk and run it from
S:Startup-Sequence.  The genuine AmigaOS 3.2.3 ROM boots first, runs
Startup-Sequence, and Startup-Sequence hands the machine to NeoBench.
That is the first step of the boot described above.

Building
--------

  make -C boot/rom

The result is boot/rom/neobench.rom, a 512 KiB kickstart-style image.
