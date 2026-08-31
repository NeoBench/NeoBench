#!/usr/bin/env python3
"""NeoBench floppy bootblock ADF builder.

Builds a bootable DD floppy (880 KB, 80 trk x 2 sides x 11 sec x 512 B)
whose track-0 bootblock is the m68k-assembled scripts/bootblock.S code.

The bootblock (per Amiga RKM) is 1024 bytes:
   0x000: 'DOS' + flags (DOS\\0 = OFS bootable)
   0x004: checksum -- carry-wraparound sum of all 256 longwords == 0xFFFFFFFF
   0x00C: boot code, loaded by Kickstart at $7C00C (A0 -> bootblock buffer,
          A1 -> IOStdReq of the reading device, A6 -> SysBase)

The boot code does not need kernel data on the floppy: it opens
scsi.device (unit 0) itself and streams the kernel image from the IDE
hardfile (images/neobench.hdf) into fast RAM at LOAD_ADDR, then jumps to
ENTRY.  The same offsets/constants as scripts/build_hdf.py are used so
the same bootblock is valid from either the floppy or the HDF partition.

Usage: python3 build_boot_adf.py <output.adf>
"""

import os
import struct
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SECTOR_SIZE = 512
ADF_SIZE = 880 * 1024
BOOTBLOCK_SIZE = 1024

KERNEL_BASE = 0x40000000


def carry_sum(data):
    """Additive carry-wraparound 32-bit sum of all longwords."""
    s = 0
    for i in range(0, len(data), 4):
        nxt = (s + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
        if nxt < s:
            nxt = (nxt + 1) & 0xFFFFFFFF
        s = nxt
    return s


def assemble_bootblock(kernel_lba, kernel_sectors, entry_target):
    """Assemble scripts/bootblock.S to a flat binary (m68k-elf tools)."""
    src = os.path.join(SCRIPT_DIR, 'bootblock.S')
    with tempfile.TemporaryDirectory() as work:
        obj = os.path.join(work, 'bb.o')
        elf = os.path.join(work, 'bb.elf')
        raw = os.path.join(work, 'bb.bin')
        defs = [
            f'KERNEL_LBA={kernel_lba}',
            f'KERNEL_SECTORS={kernel_sectors}',
            f'LOAD_ADDR={KERNEL_BASE:#x}',
            f'ENTRY={entry_target:#x}',
        ]
        subprocess.run(['m68k-elf-as', '-m68000'] +
                       [f'--defsym={d}' for d in defs] +
                       ['-o', obj, src], check=True)
        subprocess.run(['m68k-elf-ld', '-Ttext=0', '-e', '_start',
                        '-o', elf, obj], check=True)
        subprocess.run(['m68k-elf-objcopy', '-O', 'binary', '-j', '.text',
                        elf, raw], check=True)
        with open(raw, 'rb') as f:
            code = f.read()
    return code


def make_bootblock(code):
    bb = bytearray(BOOTBLOCK_SIZE)
    bb[0:4] = b'DOS\x00'                     # OFS bootable
    bb[12:12 + len(code)] = code
    bb[4:8] = struct.pack('>I', (~carry_sum(bb)) & 0xFFFFFFFF)
    return bb


def build(output_path, kernel_lba, kernel_sectors, entry_target):
    code = assemble_bootblock(kernel_lba, kernel_sectors, entry_target)
    print(f"Bootblock code: {len(code)} bytes (limit {(0x300 - 0x0C)})")
    if len(code) > 0x300 - 0x0C:
        print("ERROR: boot code overlaps IO scratch area")
        return 1

    bb = make_bootblock(code)
    chk = carry_sum(bb)
    if chk != 0xFFFFFFFF:
        print(f"ERROR: bootblock re-sum = 0x{chk:08x} != 0xffffffff")
        return 1

    adf = bytearray(ADF_SIZE)
    adf[0:BOOTBLOCK_SIZE] = bb
    with open(output_path, 'wb') as f:
        f.write(adf)

    print(f"Bootblock checksum: 0x{struct.unpack_from('>I', bb, 4)[0]:08X}")
    print(f"Whole-block re-sum: 0x{chk:08X} (expect 0xFFFFFFFF)")
    print(f"No payload on floppy: kernel is streamed from the IDE HDF "
          f"(LBA {kernel_lba}, {kernel_sectors} blocks -> jump 0x{entry_target:x})")
    print(f"ADF written: {output_path} ({len(adf)} bytes)")
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output.adf>")
        sys.exit(1)
    sys.exit(build(sys.argv[1],
                   int(os.environ.get('KERNEL_LBA', 263)),
                   int(os.environ.get('KERNEL_SECTORS', 16457)),
                   int(os.environ.get('ENTRY', '0x40001000'), 0)))