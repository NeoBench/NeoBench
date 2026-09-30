# Changelog

Notable changes to NeoBench, newest first. British English throughout.

## 0.1.3

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

### Documentation

- README: the device layer, the ZZ9000 probe, and the caveats that belong to
  both — what is proven, and what is merely implemented.
