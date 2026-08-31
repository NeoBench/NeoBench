#!/usr/bin/env python3
"""NeoBench FFS HDF builder: geometry-matched RDB + real FFS partition.

Builds a 32760-sector (130x4x63) HDF whose single partition (cyl 1..125)
is a REAL amitools-formatted FFS volume (valid root + bitmap), then splices
the NeoBench loader bootblock at partition block 0 and the kernel at the
high end of the partition (clear of the FS boot/root/bitmap area).

Usage: build_ffs_hdf.py <kernel.elf> <output.hdf>
"""

import os
import struct
import subprocess
import sys
import tempfile

from amitools.fs.blkdev.HDFBlockDevice import HDFBlockDevice
from amitools.fs.rdb.RDisk import RDisk
from amitools.fs.FSString import FSString
from amitools.fs.ADFSVolume import ADFSVolume

# this amitools has HIVE skew: PartBlockDevice passes num_blks= to the
# raw HDFBlockDevice, which predates that kwarg. shake off the kwarg.
_orig_wb = HDFBlockDevice.write_block
_orig_rb = HDFBlockDevice.read_block
HDFBlockDevice.write_block = lambda self, b, d, num_blks=None: _orig_wb(self, b, d)
HDFBlockDevice.read_block = lambda self, b, num_blks=None: _orig_rb(self, b)

CYL_BLKS = 252   # 4 heads x 63 spt
KERNEL_REL = None  # set after formatting


def bootblock_bytes(kernel_lba, kernel_sectors, load_addr, entry, root_blk):
    """Assemble bootblock.S and wrap it in a 'DOS\x01' 1024-byte bootblock.

    Bytes $00-$07 = 'DOS' + checksum; bytes $08-$0B = FFS root-block number
    (required for the Kickstart FFS mount to locate the root); code from $0C.
    """
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bootblock.S")
    with tempfile.TemporaryDirectory() as d:
        obj = os.path.join(d, "bb.o")
        binp = os.path.join(d, "bb.bin")
        subprocess.run(
            ["m68k-elf-as", "-o", obj, "-m68000",
             "--defsym", f"KERNEL_LBA={kernel_lba}",
             "--defsym", f"KERNEL_SECTORS={kernel_sectors}",
             "--defsym", f"LOAD_ADDR={load_addr}",
             "--defsym", f"ENTRY={entry}",
             src],
            check=True,
        )
        subprocess.run(
            ["m68k-elf-objcopy", "-O", "binary", "-j", ".text", obj, binp],
            check=True,
        )
        code = open(binp, "rb").read()
    assert len(code) <= 1024 - 0x0C, f"bootblock code too big: {len(code)}"
    bb = bytearray(1024)
    bb[0:4] = b"DOS\x01"
    bb[8:12] = struct.pack(">I", root_blk)
    bb[12:12 + len(code)] = code
    chk = 0
    for i in range(0, 64, 4):
        chk += struct.unpack_from(">I", bb, i)[0]
        chk &= 0xFFFFFFFF
    bb[4:8] = struct.pack(">I", (0xFFFFFFFF - chk + 1) & 0xFFFFFFFF)
    return bb


def main():
    kernel_path, out_path = sys.argv[1], sys.argv[2]
    kernel = open(kernel_path, "rb").read()
    e_entry = struct.unpack_from(">I", kernel, 24)[0]
    total_sec = 130 * CYL_BLKS
    open(out_path, "wb").write(bytearray(total_sec * 512))

    from amitools.fs.blkdev.DiskGeometry import DiskGeometry
    hdf = HDFBlockDevice(out_path, read_only=False, block_size=512)
    geo = DiskGeometry(cyls=130, heads=4, secs=63, block_bytes=512)
    hdf.create(geo)
    print("image geometry:", geo)
    rdisk = RDisk(hdf)
    rdisk.create(geo, rdb_cyls=1, hi_rdb_blk=0)
    rdisk.add_partition(
        drv_name=FSString("boot"),
        cyl_range=(1, 125),
        flags=1,            # PBFF_BOOTABLE
        boot_pri=5,
        dos_type=0x444F5301,
        fs_block_size=512,
    )

    lo_cyl, hi_cyl = rdisk.get_partition(0).get_cyl_range()
    start_lba = lo_cyl * CYL_BLKS
    part_blocks = (hi_cyl - lo_cyl + 1) * CYL_BLKS

    part = rdisk.get_partition(0)
    blkdev = part.create_blkdev()
    blkdev.open()
    vol = ADFSVolume(blkdev)
    vol.create(name=FSString("NeoBench"), dos_type=0x444F5301,
               is_ffs=True, is_intl=True, boot_code=None)
    print("volume:", vol.get_info())
    part_root = vol.root.blk_num
    vol.close()

    n_blocks = (len(kernel) + 511) // 512
    kernel_rel = part_blocks - n_blocks   # high end, clear of FS structures
    kernel_lba = start_lba + kernel_rel

    rdisk.dump()
    bb = bootblock_bytes(kernel_lba, n_blocks, 0x20000000, e_entry, part_root)
    blkdev.write_block(0, bytes(bb[0:512]))
    blkdev.write_block(1, bytes(bb[512:1024]))
    for i in range(n_blocks):
        blk = kernel[i * 512:(i + 1) * 512]
        blkdev.write_block(kernel_rel + i, blk.ljust(512, b"\x00"))
    blkdev.flush()

    print(f"partition starts at LBA {start_lba}, {part_blocks} blocks")
    print(f"kernel at LBA {kernel_lba} (rel block {kernel_rel}), {n_blocks} blocks")
    print(f"bootblock id={bytes(bb[0:4])!r}")

    rdisk.dump()
    rdisk.close()
    hdf.close()
    print("written", out_path)


if __name__ == "__main__":
    main()