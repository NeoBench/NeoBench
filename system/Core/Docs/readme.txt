NEOBENCH 0.1.6 -- m68k / AGA
============================

NeoBench is an operating system for 68060 AGA and RTG Amigas
(A1200/T and A4000/T).  The AmigaOS 3.2.3 ROM boots first and hands
the machine over; after that NeoBench runs on its own.  It does not
mount AmigaDOS volumes and it does not call into AmigaDOS libraries.

Where things live
-----------------

The store is one tree with five places at the top of it, and none of
them is borrowed from anywhere else.  Open Files and walk down from
the root to see it.

  Apps/                  programs; today they are linked into the image
  Config/                settings, read at boot straight from the image
    boot.cfg             how long the boot is held, and whether the
                         hardware is walked before drivers are bound
    pointer.cfg          mouse pointer: shape, scale, speed, colour
    screen.cfg           wallpaper, grid, glows, taskbar, bar style,
                         how much of the backdrop the bar shows, face
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

All four configuration files are read at boot, straight out of the ROM
image.  There is no configuration step: edit the file, rebuild the
ROM, reboot.

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

Building
--------

  make -C boot/rom

The result is boot/rom/neobench.rom, a 512 KiB kickstart-style image.
