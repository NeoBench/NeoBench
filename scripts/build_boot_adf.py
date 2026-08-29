#!/usr/bin/env python3
"""NeoBench bootable ADF builder.

Creates a bootable DD floppy image (880KB, 80 tracks x 2 sides x 11
sectors x 512 bytes) that mounts as floppy_drive_0 in FS-UAE.

Bootblock (sectors 0-1, 1024 bytes) layout per Amiga RKM:
   0x000: 'DOS' + flags (DOS\\0 = OFS)
   0x004: checksum  - carry-wraparound sum of all 256 longwords == 0xFFFFFFFF
   0x008: root block (unused for boot)
   0x00c: boot code - executed at $7C00C

The boot code (see bootblock.S) DoIOs a CMD_READ of NeoLoader
(neoloader-fsuae.bin, which embeds the kernel ELF) from the floppy into
$00080000 and jumps to it. NeoLoader then boots the kernel to neoshell.

Usage: python3 build_boot_adf.py <neoloader-fsuae.bin> <output.adf>
"""

import struct
import sys
import os

SECTOR_SIZE = 512
ADF_SIZE = 880 * 1024
BOOTBLOCK_SIZE = 1024

LOAD_SECTORS = 80
START_SECTOR = 2


def carry_sum(data):
    """Additive carry-wraparound 32-bit sum of all longwords."""
    s = 0
    for i in range(0, len(data), 4):
        s = (s + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
    return s


def make_bootblock(bootcode):
    """Assemble a valid 1024-byte bootblock with correct header + checksum."""
    bb = bytearray(BOOTBLOCK_SIZE)

    bb[0:4] = b'DOS\x00'           # OFS bootable

    # boot code at offset 12
    bb[12:12 + len(bootcode)] = bootcode

    # checksum field (offset 4) is currently 0; store ~sum so that
    # re-summing all 256 longwords yields 0xFFFFFFFF.
    bb[4:8] = struct.pack('>I', (~carry_sum(bb)) & 0xFFFFFFFF)

    return bb


def build(loader_path, output_path):
    with open(loader_path, 'rb') as f:
        loader = f.read()

    n_sec = (len(loader) + SECTOR_SIZE - 1) // SECTOR_SIZE
    print(f"NeoLoader: {len(loader)} bytes = {n_sec} sectors")
    if n_sec > LOAD_SECTORS:
        print(f"ERROR: loader too big for {LOAD_SECTORS} sectors")
        return 1

    adf = bytearray(ADF_SIZE)

    # boot code assembled at 0x7C00C
    with open('/tmp/opencode/bootblock.text.bin', 'rb') as f:
        bootcode = f.read()

    bb = make_bootblock(bootcode)
    adf[0:BOOTBLOCK_SIZE] = bb

    # neoloader starting at sector START_SECTOR
    start_off = START_SECTOR * SECTOR_SIZE
    adf[start_off:start_off + len(loader)] = loader

    with open(output_path, 'wb') as f:
        f.write(adf)

    # verify checksum
    chk = carry_sum(bb)
    print(f"Bootblock checksum stored: 0x{struct.unpack_from('>I', bb, 4)[0]:08X}")
    print(f"Whole-block re-sum: 0x{chk:08X} (expected 0xFFFFFFFF)")
    print(f"ADF written: {output_path} ({len(adf)} bytes)")
    print(f"NeoLoader at sector {START_SECTOR} ({hex(start_off)})")
    return 0 if (chk & 0xFFFFFFFF) == 0xFFFFFFFF else 2


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <neoloader-fsuae.bin> <output.adf>")
        sys.exit(1)
    l = sys.argv[1]
    o = sys.argv[2]
    if not os.path.exists(l):
        print(f"NeoLoader not found: {l}")
        sys.exit(1)
    sys.exit(build(l, o))
