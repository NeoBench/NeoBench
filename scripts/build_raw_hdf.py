#!/usr/bin/env python3
"""NeoBench raw-storage HDF builder (no RDB/partitions).

Kickstart then has no bootable partition on the IDE disk, so it falls
through to the floppy, whose bootblock (build_boot_adf.py) opens
scsi.device and streams the kernel out of this raw disk at KERNEL_LBA.

The kernel ELF is written flat at block KERNEL_LBA (1035) so the load
constants match scripts/bootblock.S.

Usage: python3 build_raw_hdf.py <kernel.elf> <output.hdf> [size_mb]
"""

import struct
import sys

SECTOR = 512
KERNEL_LBA = int(__import__('os').environ.get('KERNEL_LBA', '263'))

LOAD_OFFSET = 0x1000   # ELF e_entry lands at KERNEL_BASE + 0x1000
KERNEL_BASE = 0x20000000


def build(kernel_path, out_path, size_mb=16):
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    assert kernel[0:4] == b'\x7fELF' and kernel[4] == 1 and kernel[5] == 2, \
        "expect ELF32 big-endian"
    e_entry = struct.unpack_from('>I', kernel, 24)[0]
    if e_entry != KERNEL_BASE + LOAD_OFFSET:
        print(f"WARN: e_entry 0x{e_entry:x} != {KERNEL_BASE + LOAD_OFFSET:#x}")

    total_sec = (size_mb * 1024 * 1024) // SECTOR
    n_data = (len(kernel) + SECTOR - 1) // SECTOR
    print(f"Kernel: {len(kernel)} bytes, {n_data} blocks at LBA {KERNEL_LBA} "
          f"-> 0x{KERNEL_BASE:x} (load offset 0x{LOAD_OFFSET:x})")

    img = bytearray(total_sec * SECTOR)
    off = KERNEL_LBA * SECTOR
    img[off:off + len(kernel)] = kernel

    with open(out_path, 'wb') as f:
        f.write(img)

    # sanity: no RDSK magic at block 0 (Kickstart must NOT try to boot it)
    bogus_rdsk = img[0:4] == b'RDSK'
    print(f"HDF: {out_path} ({size_mb} MB), "
          f"block0={img[0:4]} (RDSK present? {bogus_rdsk})")
    return 1 if bogus_rdsk else 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <kernel.elf> <output.hdf> [size_mb]")
        sys.exit(1)
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    sys.exit(build(sys.argv[1], sys.argv[2], size))