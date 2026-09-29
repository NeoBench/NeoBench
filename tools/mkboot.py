#!/usr/bin/env python3
"""
Author a chainload hard disk image for the NeoBench bootblock.

The AmigaOS ROM will boot any device whose sector 0 starts with the
DOS\\0 magic and whose 512-byte checksum balances, and it never looks at
anything else on the disk.  That means the boot device can be authored
entirely on the host: no AmigaOS is needed to format it, and no
filesystem has to exist behind the bootblock at all.  The bootblock reads
the payload back with raw sector I/O.

The checksum the ROM verifies is the two's complement of the sum of the
block's 32-bit big-endian words with the checksum field itself treated as
zero, so that the sum over the finished block comes to exactly zero.

Usage:
    mkboot.py block.bin image.hdf [--payload kernel.bin] [--size-mb 32]
"""
import argparse
import struct
import sys

BLOCK = 512
DOS_MAGIC = b"DOS\x00"


def checksum(block):
    """Two's complement of the block's longword sum, as the ROM expects."""
    total = 0
    for i in range(0, BLOCK, 4):
        if i == 4:
            continue                     # the field itself reads as zero
        total += struct.unpack(">I", block[i:i + 4])[0]
    return (-total) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("block", help="bootblock binary, at most 512 bytes")
    ap.add_argument("image", help="hard disk image to write")
    ap.add_argument("--payload", help="NeoBench kernel image, sector 1 onward")
    ap.add_argument("--size-mb", type=int, default=32)
    args = ap.parse_args()

    with open(args.block, "rb") as fh:
        block = fh.read()

    if len(block) > BLOCK:
        sys.exit("mkboot: bootblock is %d bytes; the ROM loads %d"
                 % (len(block), BLOCK))
    if block[:4] != DOS_MAGIC:
        sys.exit("mkboot: bootblock does not start with %r" % DOS_MAGIC)

    block = block.ljust(BLOCK, b"\0")
    block = block[:4] + struct.pack(">I", checksum(block)) + block[8:]

    # Belt and braces: the ROM must be able to add this up and reach zero.
    total = sum(struct.unpack(">I", block[i:i + 4])[0]
                for i in range(0, BLOCK, 4)) & 0xFFFFFFFF
    if total != 0:
        sys.exit("mkboot: checksum does not balance (0x%08x)" % total)

    size = args.size_mb * 1024 * 1024
    image = bytearray(size)
    image[0:BLOCK] = block

    if args.payload:
        with open(args.payload, "rb") as fh:
            payload = fh.read()
        if BLOCK + len(payload) > size:
            sys.exit("mkboot: payload does not fit in %d MB" % args.size_mb)
        image[BLOCK:BLOCK + len(payload)] = payload

    with open(args.image, "wb") as fh:
        fh.write(image)

    sys.stderr.write(
        "mkboot: %s, %d MB, bootblock checksum 0x%08x, payload %d bytes\n"
        % (args.image, args.size_mb,
           struct.unpack(">I", block[4:8])[0],
           len(args.payload) if args.payload else 0))


if __name__ == "__main__":
    main()
