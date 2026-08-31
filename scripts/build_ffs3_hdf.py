#!/usr/bin/env python3
"""NeoBench FFS RDB builder (v3): Kickstart-mountable FFS root.

Reuses the geometry/RDB/bootcode from build_hdf.py but replaces the FS stub
with a CANONICAL FFS root block (amitools layout: type=0, header=2, hash of
72 longs, bm_flag/bm_ptrs/bm_ext/ts/name, sum-to-zero checksum) plus real
bitmap blocks.  The partition bootblock now carries the FFS root-block number
at bytes $08-$0B, which is what Kickstart's FFS mount resolves before it
stages the bootblock for execution.

Layout (partition-relative):
   0-1    our DOS\\1 1024-byte bootblock (KERNEL_LBA=263)
   2..10  unused
   11..   kernel data (n_data blocks; LBA 263 = ffs_lba+11)
   root_rel..  FFS root block
   ..          bitmap blocks

Usage: build_ffs3_hdf.py <kernel.elf> <output.hdf>
"""

import math
import struct
import sys

sys.path.insert(0, "/home/lordp/NeoBench/scripts")
import build_hdf as bh

SECTOR = 512
GEO = (170, 4, 63)              # cyls, heads, spt
BPC = 252
FFS_CYL0, FFS_CYL1 = 1, 166     # partition cylinders
FFS_LBA = FFS_CYL0 * BPC        # 252
FFS_BLOCKS = (FFS_CYL1 - FFS_CYL0 + 1) * BPC
ROOT_BMP_BLOCKS = 25            # max bm_ptrs
BITS_PER_BMP = (SECTOR - 16) * 8
ROOT_REL = FFS_BLOCKS // 2      # Kickstart mounts root at partition size/2


def w32(buf, off, val):
    struct.pack_into(">I", buf, off, val)


def root_block(rel, name=b"NeoBench"):
    rb = bytearray(SECTOR)
    # amitools RootBlock offsets (block_longs = 128):
    #   0 type | 2*4 header(ST_ROOT) | 3*4 hash_size | 6..5+hs hash
    #   -50 bm_flag | -49..-25 bm_ptrs | -24 bm_ext | -23 mod_ts
    #   -20 name | -11 blocks_used | -10 disk_ts | -7 create_ts
    #   -2 extension | -4 fstype
    w32(rb, 4, 2)                        # ST_ROOT
    w32(rb, 12, 72)                      # hash size (block_longs-56)
    w32(rb, 312, 0xFFFFFFFF)             # bm_flag: bitmap present/valid
    for i in range(ROOT_BMP_BLOCKS):
        w32(rb, 316 + i * 4, bmp_rel(rel) + i)
    w32(rb, 416, 0)                      # no bitmap extension
    rb[432:432 + 1 + len(name)] = bytes([len(name)]) + name
    w32(rb, 504, 0)                      # extension pointer
    w32(rb, 508, 0)                      # fstype (DOS6/7 only)
    w32(rb, 8, bh.rdb_csum(rb, SECTOR))
    return rb


def bmp_rel(root_rel):
    """first partition-relative bitmap block, right after the root."""
    return root_rel + 1


def bitmap_block(base_rel, n_blocks, used):
    bm = bytearray(SECTOR)
    w32(bm, 12, 0)                          # bm_seq (0 = seq 0)
    for b in used:
        if base_rel <= b < base_rel + BITS_PER_BMP and b < n_blocks:
            i = b - base_rel
            bm[16 + i // 8] |= 0x80 >> (i % 8)
    w32(bm, 8, bh.rdb_csum(bm, SECTOR))
    return bm


def main():
    kernel_path, out_path = sys.argv[1], sys.argv[2]
    kernel = open(kernel_path, "rb").read()
    e_entry = struct.unpack_from(">I", kernel, 24)[0]
    n_data = (len(kernel) + SECTOR - 1) // SECTOR

    total_sec = GEO[0] * BPC
    img = bytearray(total_sec * SECTOR)

    # ---------------- RDSK + PART (proven helpers from build_hdf) --------
    bh.GEOMETRY = GEO
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
    p1 = bh.make_part(next_block=0xFFFFFFFF, dostype=0x444F5301,
                      name=b'boot', lowcyl=FFS_CYL0, highcyl=FFS_CYL1,
                      bootpri=5, heads=GEO[1], spt=GEO[2])
    img[SECTOR:2 * SECTOR] = p1

    # ---------------- kernel at LBA 263 (rel 11) -------------------------
    cy0 = 2 + 8 if False else 2            # not used
    KERNEL_REL = 11
    kernel_lba = FFS_LBA + KERNEL_REL
    if KERNEL_REL + n_data > FFS_BLOCKS:
        raise SystemExit("kernel too large")
    for i in range(n_data):
        blk = kernel[i * SECTOR:(i + 1) * SECTOR]
        off = (kernel_lba + i) * SECTOR
        img[off:off + SECTOR] = blk.ljust(SECTOR, b"\x00")

    # ---------------- FFS root + bitmaps above the kernel ----------------
    root_rel = ROOT_REL
    root_lba = FFS_LBA + root_rel
    img[root_lba * SECTOR:(root_lba + 1) * SECTOR] = root_block(root_rel)

    n_bmp = math.ceil(FFS_BLOCKS / BITS_PER_BMP)
    if n_bmp > 25:
        raise SystemExit(f"too many bitmap blocks: {n_bmp}")
    used = [0, 1] + list(range(2, 2 + 16))  # boot/reserved/large reserve
    used += list(range(KERNEL_REL, KERNEL_REL + n_data))
    used += [root_rel + k for k in range(n_bmp + 1)]
    for k in range(n_bmp):
        rel = bmp_rel(root_rel) + k
        img[(FFS_LBA + rel) * SECTOR:(FFS_LBA + rel + 1) * SECTOR] = \
            bitmap_block(rel, FFS_BLOCKS, used)

    # ---------------- our DOS\\1 bootblock with root pointer -------------
    bc = bh.assemble_bootblock(kernel_lba, n_data, e_entry)
    bb = bytearray(2 * SECTOR)
    bb[0:4] = b'DOS\x01'
    w32(bb, 8, root_rel)                 # FFS root block (partition-relative)
    bb[0x0C:0x0C + len(bc)] = bc
    w32(bb, 4, bh.bootblk_csum(bb, 2 * SECTOR))
    off = FFS_LBA * SECTOR
    img[off:off + 2 * SECTOR] = bb

    open(out_path, "wb").write(img)

    ok = True

    def sumz(name, data):
        s = 0
        for i in range(0, len(data), 4):
            s = (s + struct.unpack_from(">I", data, i)[0]) & 0xFFFFFFFF
        good = (s == 0)
        print(f"{name}: sum=0x{s:08x} {'OK' if good else 'BAD'}")
        return good

    ok &= sumz("RDSK", img[0:SECTOR])
    ok &= sumz("PART", img[SECTOR:2 * SECTOR])
    ok &= sumz("FFS root", img[root_lba * SECTOR:(root_lba + 1) * SECTOR])
    ok &= sumz("bitmap0", img[(FFS_LBA + bmp_rel(root_rel)) * SECTOR:(FFS_LBA + bmp_rel(root_rel) + 1) * SECTOR])
    rsum = bh.carry_wrap_sum(bytes(bb), 2 * SECTOR)
    ok &= (rsum == 0xFFFFFFFF) and True
    print(f"bootblock checksum re-sum: 0x{rsum:08x} "
          f"{'OK (== ffffffff)' if rsum == 0xFFFFFFFF else 'BAD'}")
    print(f"kernel: {len(kernel)}B {n_data} blk @ LBA {kernel_lba} (rel {KERNEL_REL})")
    print(f"FFS root at LBA {root_lba} (rel {root_rel}), bitmap x{n_bmp}")
    print(f"e_entry={e_entry:#x}")
    print("ALL CHECKS", "OK" if ok else "FAILED")


if __name__ == "__main__":
    main()