#!/usr/bin/env python3
"""NeoBench FFS RDB builder (v4): Kickstart-exact FFS via RAM block device.

Kickstart's scsi/FFS mount reads the partition FILESYSTEM ROOT at the
partition's own `num_blocks/2` block (never our bootblock $08), and needs a
fully valid FFS (root dir, bitmaps, bootblock) there -- the malformed manual
root of v3 was rejected.  amitools computes the same root location, so we let
amitools FORMAT the whole filesystem into a RAM block device (correct
position-independent relative block numbering, self-consistent dot entries,
bitmap pointers), splice OUR loader bootblock at blocks 0-1, then dump the
produced blocks into the HDF partition and park the kernel at LBA 263.

Layout (partition-relative, 170-cyl disk -> partition cyls 1..166):
   0-1   our DOS\x01 loader bootblock ($08 rootptr = 20916)
   11..  kernel (16457 blocks, LBA 263)
   20916 FFS root (num_blocks/2), bitmaps/dir around it
   the rest of the amitools-formatted FS

Usage: build_ffs4_hdf.py <kernel.elf> <output.hdf>
"""

import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "/home/lordp/NeoBench/scripts")
import build_hdf as bh
from amitools.fs.blkdev.BlockDevice import BlockDevice
from amitools.fs.FSString import FSString
from amitools.fs.ADFSVolume import ADFSVolume

SECTOR = 512
GEO = (170, 4, 63)              # cyls, heads, spt
BPC = 252
FFS_CYL0, FFS_CYL1 = 1, 166
FFS_LBA = FFS_CYL0 * BPC
FFS_BLOCKS = (FFS_CYL1 - FFS_CYL0 + 1) * BPC
CYL_START, CYL_END = (FFS_CYL0, FFS_CYL1)
KERNEL_REL = 11


class RamBlkDev(BlockDevice):
    """in-memory block device whose block numbers are partition-relative."""

    def __init__(self, cyls, heads, sectors, block_bytes=512):
        self._set_geometry(cyls, heads, sectors, block_bytes, reserved=2)
        self.blocks = [bytearray(block_bytes) for _ in range(self.num_blocks)]
        self.total = 0

    def read_block(self, blk_num):
        return bytes(self.blocks[blk_num])

    def write_block(self, blk_num, data):
        if len(data) < self.block_bytes:
            data = data + bytes(self.block_bytes - len(data))
        self.blocks[blk_num][:self.block_bytes] = data[:self.block_bytes]
        self.total += 1

    def flush(self):
        pass


def assemble_bootblock(kernel_lba, kernel_sectors, entry, root_blk):
    """our DOS\x01 loader bootblock with the FFS root pointer at $08."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bootblock.S")
    with tempfile.TemporaryDirectory() as d:
        obj = os.path.join(d, "bb.o")
        binp = os.path.join(d, "bb.bin")
        subprocess.run(
            ["m68k-elf-as", "-o", obj, "-m68000",
             "--defsym", f"KERNEL_LBA={kernel_lba}",
             "--defsym", f"KERNEL_SECTORS={kernel_sectors}",
             "--defsym", f"LOAD_ADDR={0x40000000}",
             "--defsym", f"ENTRY={entry}",
             src], check=True)
        subprocess.run(["m68k-elf-objcopy", "-O", "binary", "-j", ".text",
                        obj, binp], check=True)
        code = open(binp, "rb").read()
    bb = bytearray(2 * SECTOR)
    bb[0:4] = b"DOS\x01"
    struct.pack_into(">I", bb, 8, root_blk)
    bb[0x0C:0x0C + len(code)] = code
    struct.pack_into(">I", bb, 4, bh.bootblk_csum(bb, 2 * SECTOR))
    return bb


def main():
    kernel_path, out_path = sys.argv[1], sys.argv[2]
    kernel = open(kernel_path, "rb").read()
    e_entry = struct.unpack_from(">I", kernel, 24)[0]
    n_data = (len(kernel) + SECTOR - 1) // SECTOR
    if KERNEL_REL + n_data >= FFS_BLOCKS // 2:
        raise SystemExit("kernel collides with mid-partition FFS root")

    img = bytearray(GEO[0] * BPC * SECTOR)

    # ---------- RDSK + PART (proven helpers) ----------
    rdb = bh.make_rdsk(hostid=7, blockbytes=SECTOR, flags=0x07,
                       badblocklist=0xFFFFFFFF, partitionlist=1,
                       fsyslist=0xFFFFFFFF, drive_init=0xFFFFFFFF,
                       rdbblocks_lo=0, rdbblocks_hi=BPC - 1,
                       cylinders=GEO[0], sectors=GEO[2], heads=GEO[1],
                       interleave=1, park=GEO[0] - 1,
                       lowcyl=FFS_CYL0, highcyl=GEO[0] - 1,
                       cylblocks=BPC, high_rdsk=0,
                       wpc=GEO[0] - 1, rw=GEO[0] - 1, step=3)
    img[0:SECTOR] = rdb
    img[SECTOR:2 * SECTOR] = bh.make_part(
        next_block=0xFFFFFFFF, dostype=0x444F5301, name=b'boot',
        lowcyl=FFS_CYL0, highcyl=FFS_CYL1, bootpri=5,
        heads=GEO[1], spt=GEO[2])

    # ---------- kernel at LBA 263 ----------
    for i in range(n_data):
        blk = kernel[i * SECTOR:(i + 1) * SECTOR]
        off = (FFS_LBA + KERNEL_REL + i) * SECTOR
        img[off:off + SECTOR] = blk.ljust(SECTOR, b"\x00")

    # ---------- amitools formats a real FFS into the RAM partition ----------
    ram = RamBlkDev(FFS_CYL1 - FFS_CYL0 + 1, GEO[1], GEO[2])
    vol = ADFSVolume(ram)
    vol.create(name=FSString("NeoBench"), dos_type=0x444F5301,
               is_ffs=True, is_intl=True, boot_code=None)
    print("formatted:", vol.get_info(), "root_blk:", vol.root.blk_num)
    root_blk = vol.root.blk_num
    if root_blk != FFS_BLOCKS // 2:
        print(f"WARN: amitools root {root_blk}, Kickstart expects {FFS_BLOCKS//2}")
    vol.close()

    # ---------- splice our loader bootblock ----------
    bb = assemble_bootblock(FFS_LBA + KERNEL_REL, n_data, e_entry, root_blk)
    ram.blocks[0] = bytearray(bb[0:SECTOR])
    ram.blocks[1] = bytearray(bb[SECTOR:2 * SECTOR])

    # dump partition-relative FS blocks onto the image, avoiding the kernel
    # region (rel 11..11+16457).  amitools only used boot(0-1) + root/bitmaps
    # around 20916, so dump the low tail and the high cluster.
    dump_ranges = [(0, KERNEL_REL), (root_blk - 2, root_blk + 32)]
    for lo, hi in dump_ranges:
        for rel in range(max(0, lo), min(hi, FFS_BLOCKS)):
            off = (FFS_LBA + rel) * SECTOR
            img[off:off + SECTOR] = ram.blocks[rel]
    open(out_path, "wb").write(img)

    # ---------- verification ----------
    ok = True
    for name, blk in (("RDSK", 0), ("PART", 1)):
        s = 0
        d = img[blk * SECTOR:(blk + 1) * SECTOR]
        for i in range(0, SECTOR, 4):
            s = (s + struct.unpack_from(">I", d, i)[0]) & 0xFFFFFFFF
        print(f"{name}: sum=0x{s:08x} {'OK' if s == 0 else 'BAD'}")
        ok &= s == 0
    rb = img[(FFS_LBA + root_blk) * SECTOR:(FFS_LBA + root_blk + 1) * SECTOR]
    s = 0
    for i in range(0, SECTOR, 4):
        s = (s + struct.unpack_from(">I", rb, i)[0]) & 0xFFFFFFFF
    print(f"FFS root@{root_blk}: sum=0x{s:08x} {'OK' if s == 0 else 'BAD'}")
    ok &= s == 0
    bw = bb[0:4] + struct.pack(">I", bb[4:8] and bb[8:12][0])
    rsum = bh.carry_wrap_sum(bytes(bb), 2 * SECTOR)
    ok &= rsum == 0xFFFFFFFF
    print(f"bootblock csum: 0x{rsum:08x} {'OK' if rsum == 0xFFFFFFFF else 'BAD'}")
    got = bytes(img[(FFS_LBA + KERNEL_REL) * SECTOR:(FFS_LBA + KERNEL_REL) * SECTOR + 4])
    ok &= got == bytes(kernel[0:4])
    print(f"kernel head at LBA {FFS_LBA + KERNEL_REL}: {got.hex()} {'OK' if got == bytes(kernel[0:4]) else 'BAD'}")
    print("written", out_path, "ALL CHECKS", "OK" if ok else "FAILED")


if __name__ == "__main__":
    main()