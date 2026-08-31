#!/usr/bin/env python3
"""Build a bootblock-only ADF from scripts/boottest.S (serial test)."""

import os
import struct
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ADF_SIZE = 880 * 1024
BOOTBLOCK_SIZE = 1024


def carry_sum(data):
    s = 0
    for i in range(0, len(data), 4):
        nxt = (s + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
        if nxt < s:
            nxt = (nxt + 1) & 0xFFFFFFFF
        s = nxt
    return s


def assemble(src):
    with tempfile.TemporaryDirectory() as work:
        obj = os.path.join(work, 'bb.o')
        elf = os.path.join(work, 'bb.elf')
        raw = os.path.join(work, 'bb.bin')
        subprocess.run(['m68k-elf-as', '-m68000', '-o', obj, src], check=True)
        subprocess.run(['m68k-elf-ld', '-Ttext=0', '-e', '_start',
                        '-o', elf, obj], check=True)
        subprocess.run(['m68k-elf-objcopy', '-O', 'binary', '-j', '.text',
                        elf, raw], check=True)
        with open(raw, 'rb') as f:
            return f.read()


def main(src, out):
    code = assemble(src)
    print(f"Bootblock code: {len(code)} bytes")
    bb = bytearray(BOOTBLOCK_SIZE)
    bb[0:4] = b'DOS\x00'
    bb[12:12 + len(code)] = code
    bb[4:8] = struct.pack('>I', (~carry_sum(bb)) & 0xFFFFFFFF)
    chk = carry_sum(bb)
    if chk != 0xFFFFFFFF:
        print(f"ERROR: re-sum = 0x{chk:08x}")
        return 1
    adf = bytearray(ADF_SIZE)
    adf[0:BOOTBLOCK_SIZE] = bb
    with open(out, 'wb') as f:
        f.write(adf)
    print(f"ADF written: {out} ({len(adf)} bytes), checksum 0x{chk:08x}")
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bootblock.S> <out.adf>")
        sys.exit(1)
    sys.exit(main(sys.argv[1], sys.argv[2]))