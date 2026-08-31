#!/usr/bin/env python3
"""NeoBench HDF Builder v8 — canonical RDB, DOS bootblock, assembled 68k.

Fixes vs v7 (all verified against ACE hardblocks.h + amitools):
  * RDSK (RigidDiskBlock) lives at block 0 with canonical field offsets.
  * PART block at block 1 with canonical offsets and a full 20-long
    struct DosEnvec environment at offset 128 (pb_Environment).
  * RDSK/PART/FFS block checksums sum-to-zero (RW = -sum).
  * Bootblock checksum is the end-around-carry sum -> 0xFFFFFFFF.
  * Boot code is assembled from scripts/bootblock.S with m68k-elf
    binutils and inserted at offset $0C (not $08).
  * IOStdReq offsets corrected: io_Command=28, io_Length=36, io_Data=40,
    io_Offset=44 (byte offsets for scsi.device). Kernel read is chunked
    128 KB per DoIO.
"""

import os
import struct
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SECTOR = 512

KERNEL_BASE = 0x40000000      # Zorro III fast RAM (A4000, FS-UAE autoconfig)

# NeoBench native filesystem — "NBFS". Proclaimed to Kickstart as a custom,
# Unix-style filesystem via an RDB FileSystemHeader ("neobench.fsh") rather
# than being faked as an Amiga FFS volume. The partition advertises this
# DosType so OS 3.x mounting hands the bootblock straight to the kernel.
NBFS_DOSTYPE = 0x4E424653    # "NBFS\0" (big-endian ASCII identifier)

# Classic m68k AmigaOS binary IOStdReq offsets
IO_COMMAND = 28
IO_ERROR = 31
IO_LENGTH = 36
IO_DATA = 40
IO_OFFSET = 44


def w32(d, o, v):
    struct.pack_into('>I', d, o, v & 0xFFFFFFFF)


def rdb_csum(data, nbytes=None):
    """RDB/FFS block checksum: longword sum to zero (checksum = -sum)."""
    if nbytes is None:
        nbytes = len(data)
    s = 0
    for i in range(0, nbytes, 4):
        s = (s + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
    return (-s) & 0xFFFFFFFF


def carry_wrap_sum(data, nbytes):
    """Additive carry-wraparound 32-bit sum used by bootblock checksums."""
    s = 0
    for i in range(0, nbytes, 4):
        v = struct.unpack_from('>I', data, i)[0]
        nxt = (s + v) & 0xFFFFFFFF
        if nxt < s:
            nxt = (nxt + 1) & 0xFFFFFFFF
        s = nxt
    return s


def bootblk_csum(data, nbytes):
    """Bootblock checksum: store ~(carry-wrapped sum); total == 0xFFFFFFFF."""
    return (~carry_wrap_sum(data, nbytes)) & 0xFFFFFFFF


# ---------------------------------------------------------------------
# scripts/bootblock.S  ->  flat binary
# ---------------------------------------------------------------------
def assemble_bootblock(kernel_lba, kernel_sectors, entry_target):
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


# ---------------------------------------------------------------------
# RigidDiskBlock (devices/hardblocks.h offset table)
# ---------------------------------------------------------------------
def make_rdsk(hostid, blockbytes, flags, badblocklist, partitionlist,
              fsyslist, drive_init, rdbblocks_lo, rdbblocks_hi,
              cylinders, sectors, heads, interleave, park,
              lowcyl, highcyl, cylblocks, high_rdsk, wpc, rw, step):
    # Layout mirrors amitools/rdbtool output byte-for-byte (ground truth:
    # the same RDB is produced by HDInstTool-class tools and mounts under
    # Kickstart 3.2).  rdb_SummedLongs == 64 (256-byte header region).
    rdb = bytearray(SECTOR)
    w32(rdb, 0, 0x5244534B)                  # rdb_ID "RDSK"
    w32(rdb, 4, 64)                          # rdb_SummedLongs
                                             # rdb_ChkSum @8 (last)
    w32(rdb, 12, hostid)                     # rdb_HostID
    w32(rdb, 16, blockbytes)                 # rdb_BlockBytes
    w32(rdb, 20, flags)                      # rdb_Flags
    w32(rdb, 24, badblocklist)               # rdb_BadBlockList (0xFFFFFFFF)
    w32(rdb, 28, partitionlist)              # rdb_PartitionList
    w32(rdb, 32, fsyslist)                   # rdb_FileSysHeaderList (0xFFFFFFFF)
    w32(rdb, 36, drive_init)                 # rdb_DriveInit (0xFFFFFFFF)
                                             # rdb_BootBlk @40 = 0
                                             # rdb_BockList @44 = 0
                                             # rdb_Reserved1[] zeros @48
    # physical drive geometry (matches rdbtool Reserved2 layout)
    w32(rdb, 64, cylinders)                  # cyls
    w32(rdb, 68, sectors)                    # secs
    w32(rdb, 72, heads)                      # heads
    w32(rdb, 76, interleave)                 # interleave
    w32(rdb, 80, park)                       # parking_zone
    w32(rdb, 96, wpc)                        # write_pre_comp
    w32(rdb, 100, rw)                        # reduced_write
    w32(rdb, 104, step)                      # step_rate
    # logical drive geometry (matches rdbtool Reserved3 layout)
    w32(rdb, 128, rdbblocks_lo)              # rdb_blk_lo
    w32(rdb, 132, rdbblocks_hi)              # rdb_blk_hi (last RDB block)
    w32(rdb, 136, lowcyl)                    # lo_cyl (data starts here)
    w32(rdb, 140, highcyl)                   # hi_cyl
    w32(rdb, 144, cylblocks)                 # cyl_blocks
                                             # auto_park_secs @148 = 0
    w32(rdb, 152, high_rdsk)                 # high_rdsk_block
    w32(rdb, 8, rdb_csum(rdb, SECTOR))
    return rdb


# ---------------------------------------------------------------------
# PartitionBlock: canonical DosEnvec at offset 128 (amitools layout)
# ---------------------------------------------------------------------
def make_part(next_block, dostype, name, lowcyl, highcyl, bootpri,
              heads, spt):
    # Mirrors amitools/rdbtool PartitionBlock byte-for-byte.  The
    # DosEnvec slots matter; blk_per_trk must stay at slot 5 (my v8 put
    # dostype there, which Kickstart rejected).
    p = bytearray(SECTOR)
    w32(p, 0, 0x50415254)                    # pb_ID "PART"
    w32(p, 4, 64)                            # pb_SummedLongs (256-byte region)
                                             # pb_ChkSum @8 (last)
    w32(p, 12, 7)                            # pb_HostID
    w32(p, 16, next_block)                   # pb_Next (0xFFFFFFFF = none)
    w32(p, 20, 0x01)                         # pb_Flags PBFF_BOOTABLE
    w32(p, 24, 0)                            # pb_DevFlags
    # pb_DriveName: ubyte length followed by the name chars (amitools
    # convention; FS-UAE null-terminates at 37+len, so a missing length
    # byte makes it clobber the DosEnvec below).
    name = name[:31]
    p[36] = len(name)
    p[37:37 + len(name)] = name
                                             # else zeros @68..127

    env = [0] * 20
    env[0] = 16                              # DE_TABLESIZE
    env[1] = 128                             # DE_SIZEBLOCK (bytes; amitools)
    env[2] = 0                               # DE_SECORG
    env[3] = heads                           # DE_NUMHEADS
    env[4] = 1                               # DE_SECSPERTRACK (1 sec/block)
    env[5] = spt                             # DE_BLKSPERTRACK
    env[6] = 2                               # DE_RESERVED (amitools value)
    env[7] = 0                               # DE_PREALLOC
    env[8] = 0                               # DE_INTERLEAVE
    env[9] = lowcyl                          # DE_LOWCYL
    env[10] = highcyl                        # DE_HIGHCYL
    env[11] = 30                             # DE_NUMBUFFER
    env[12] = 0                              # DE_BUFMEMTYPE
    env[13] = 0x00FFFFFF                     # DE_MAXTRANSFER
    env[14] = 0x7FFFFFFE                     # DE_MASK
    env[15] = bootpri                        # DE_BOOTPRI
    env[16] = dostype                        # DE_DOSTYPE
    env[17] = 0                              # DE_BAUD
    env[18] = 0                              # DE_CONTROL
    env[19] = 0                              # DE_BOOTBLOCKS
    for i, v in enumerate(env):
        w32(p, 128 + i * 4, v)

    w32(p, 8, rdb_csum(p, SECTOR))
    return p


# ---------------------------------------------------------------------
# FileSystemHeader (FSHD): announces the NeoBench "neobench.fsh" native
# filesystem to Kickstart (dosextens.h struct FileSysHeaderBlock).  The
# rom reads the RDB's rdb_FileSysHeaderList, matches fs_DosType against the
# partition's DE_DOSTYPE, and uses the declared handler to mount the volume
# so it can reach the bootblock and hand control to the kernel.
# ---------------------------------------------------------------------
def make_fshd(next_block, dostype, device, handler, hostid=7):
    f = bytearray(SECTOR)
    w32(f, 0, 0x46534844)                  # fs_ID "FSHD"
    w32(f, 4, 64)                          # fs_SummedLongs
                                           # fs_ChkSum @8 (last)
    w32(f, 12, hostid)                     # fs_HostID
    w32(f, 16, next_block)                 # fs_Next (0xFFFFFFFF = none)
    w32(f, 20, 0x02)                       # fs_Flags FSSF_BOOTBLOCKRESIDENT
    dev = device[:31]
    f[24] = len(dev)                       # fs_Device (BSTR)
    f[25:25 + len(dev)] = dev
    hnd = handler[:31]
    f[56] = len(hnd)                       # fs_FileName (BSTR)
    f[57:57 + len(hnd)] = hnd
    w32(f, 88, dostype)                    # fs_DosType
                                           # fs_Reserved[] zeros
                                           # fs_SegListBlocks @104
    w32(f, 8, rdb_csum(f, SECTOR))
    return f


# ---------------------------------------------------------------------
# Raw NBFS partition builder (no FFS stub): the bootblock streams the
# kernel directly and the RDB FSHD declares the volume as "neobench.fsh".
# ---------------------------------------------------------------------
def build(kernel_path, out_path, size_mb=16):
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    # ---- ELF entry -> flat load offset at KERNEL_BASE ----
    assert kernel[0:4] == b'\x7fELF' and kernel[4] == 1 and kernel[5] == 2, \
        "expect ELF32 big-endian"
    e_entry = struct.unpack_from('>I', kernel, 24)[0]
    phoff = struct.unpack_from('>I', kernel, 28)[0]
    phentsize = struct.unpack_from('>H', kernel, 42)[0]
    phnum = struct.unpack_from('>H', kernel, 44)[0]
    entry_off = None
    for i in range(phnum):
        ph = phoff + i * phentsize
        p_type, p_offset, p_vaddr = struct.unpack_from('>III', kernel, ph)
        p_memsz = struct.unpack_from('>I', kernel, ph + 16)[0]
        if p_type == 1 and p_vaddr <= e_entry < p_vaddr + max(p_memsz, 1):
            entry_off = p_offset + (e_entry - p_vaddr)
            break
    if entry_off is None:
        print("ERROR: could not locate entry point in any PT_LOAD segment")
        sys.exit(1)
    entry_target = KERNEL_BASE + entry_off
    print(f"Kernel ELF: e_entry=0x{e_entry:x} -> load offset 0x{entry_off:x} "
          f"-> jump target 0x{entry_target:08x}")

    # Drive-matched geometry: FS-UAE identifies the 16 MB IDE image as
    # LCHS=130/4/63 (32760 sectors).  The RDB/partition must be within
    # that; Kickstart's scsi.device rejects a partition whose CHS
    # geometry exceeds the drive's IDENTIFY geometry.
    heads = 4
    spt = 63
    bpc = heads * spt                        # 252 sectors/cylinder
    total_cyls = 130                         # 130 * 252 = 32760 sectors
    total_sec = total_cyls * bpc
    img = bytearray(total_sec * SECTOR)

    part_start_cyl = 1
    part_end_cyl = 125
    part_lba = part_start_cyl * bpc            # partition owns cylinders 1..125

    n_data = (len(kernel) + SECTOR - 1) // SECTOR

    # Partition-relative layout (raw NBFS, no FFS stub):
    #   0-1 bootblock, 2..2+n_data-1 kernel data
    REL_KERNEL = 2
    kernel_lba = part_lba + REL_KERNEL
    root_rel = REL_KERNEL + n_data             # where the NBFS superblock lives
    part_blocks = (part_end_cyl - part_start_cyl + 1) * bpc
    if root_rel >= part_blocks:
        print(f"ERROR: kernel too large for NBFS partition "
              f"({root_rel + 1} > {part_blocks} blocks)")
        sys.exit(1)

    print(f"Kernel: {len(kernel)} bytes, {n_data} blocks at LBA {kernel_lba}")
    print(f"Geometry: {total_cyls} cyls, {heads} heads, {spt} spt, {bpc} sec/cyl")

    # ==================================================================
    # RDSK at block 0 (canonical; HDToolBox/WinUAE write it at sector 0)
    # ==================================================================
    rdb = make_rdsk(
        hostid=7, blockbytes=SECTOR, flags=0x07,          # RDBFF_SYNCFS|NOREMOVE|LAST
        badblocklist=0xFFFFFFFF, partitionlist=1,
        fsyslist=2, drive_init=0xFFFFFFFF,
        rdbblocks_lo=0, rdbblocks_hi=bpc - 1,             # 251 (RDB zone = cyl 0)
        cylinders=total_cyls, sectors=spt, heads=heads,
        interleave=1, park=total_cyls - 1,
        lowcyl=1, highcyl=total_cyls - 1,
        cylblocks=bpc, high_rdsk=0,
        wpc=total_cyls - 1, rw=total_cyls - 1, step=3,
    )
    img[0:SECTOR] = rdb

    # ==================================================================
    # PART at block 1 (bootable, NeoBench NBFS, cylinders 1..125)
    # ==================================================================
    p1 = make_part(next_block=0xFFFFFFFF, dostype=NBFS_DOSTYPE, name=b'boot',
                   lowcyl=part_start_cyl, highcyl=part_end_cyl, bootpri=5,
                   heads=heads, spt=spt)
    img[SECTOR:2 * SECTOR] = p1

    # ==================================================================
    # FileSystemHeader at block 2: proclaim "neobench.fsh" (NBFS) so the
    # 3.2+ Kickstart recognises the volume as the native NeoBench
    # filesystem and mounts it to reach the bootblock -> kernel handover.
    # ==================================================================
    fsh = make_fshd(next_block=0xFFFFFFFF, dostype=NBFS_DOSTYPE,
                    device=b'NEObench0', handler=b'neobench.fsh')
    img[2 * SECTOR:3 * SECTOR] = fsh

    # ==================================================================
    # Bootblock at partition start (part_lba): NBFS bootblock, code at $0C.
    # The mounted volume's boot block is what hands control to the kernel.
    # ==================================================================
    bc = assemble_bootblock(kernel_lba, n_data, entry_target)
    if len(bc) > 0x300 - 0x0C:
        print(f"ERROR: boot code {len(bc)} bytes > {(0x300 - 0x0C)} (overlaps IO scratch)")
        sys.exit(1)
    bb = bytearray(2 * SECTOR)
    bb[0:4] = b'DOS\x01'                 # DOS bootable ID (ROM executes it to
                                         # hand control to the kernel)
    bb[0x0C:0x0C + len(bc)] = bc
    w32(bb, 4, bootblk_csum(bb, 2 * SECTOR))
    off = part_lba * SECTOR
    img[off:off + 2 * SECTOR] = bb
    print(f"Bootblock: {len(bc)} bytes of 68k code at LBA {part_lba}")

    # ==================================================================
    # flat kernel data (raw, immediately after the bootblock)
    # ==================================================================
    img[(part_lba + REL_KERNEL) * SECTOR:(part_lba + REL_KERNEL) * SECTOR +
        len(kernel)] = kernel

    # ==================================================================
    # write output
    # ==================================================================
    with open(out_path, 'wb') as f:
        f.write(img)

    print(f"\nHDF: {out_path} ({size_mb} MB)")
    print(f"RDSK:       LBA 0")
    print(f"FSHD:       LBA 2 (neobench.fsh, dos type {NBFS_DOSTYPE:#x})")
    print(f"PART 1:     LBA 1 (bootable NBFS, cyl {part_start_cyl}-{part_end_cyl}, "
          f"{part_blocks} blocks, boot_pri 5)")
    print(f"Partition:  cyl {part_start_cyl} * {bpc} sec/cyl = start LBA {part_lba}")
    print(f"Kernel:     LBA {kernel_lba} ({n_data} blocks, raw, no FFS)")
    print(f"Root:       LBA {part_lba + root_rel}")

    # ==================================================================
    # verify
    # ==================================================================
    print("\n=== Verification ===")

    def sum_block(buf, nbytes, label, expect):
        s = 0
        for i in range(0, nbytes, 4):
            s = (s + struct.unpack_from('>I', buf, i)[0]) & 0xFFFFFFFF
        ok = s == expect
        print(f"{label}: re-sum = 0x{s:08x} (expect 0x{expect:08x}) -> "
              f"{'OK' if ok else 'FAIL'}")
        return ok

    ok = True
    ok &= sum_block(img[0:SECTOR], SECTOR, "RDSK block 0 checksum", 0)
    ok &= sum_block(img[SECTOR:2 * SECTOR], SECTOR, "PART block 1 checksum", 0)
    ok &= sum_block(img[2 * SECTOR:3 * SECTOR], SECTOR, "FSHD block 2 checksum", 0)

    r = img[0:SECTOR]
    print(f"RDSK ID: {r[0:4]} (expect b'RDSK')", end="  ")
    print(f"PartitionList={struct.unpack_from('>I', r, 28)[0]} "
          f"(expect 1) FileSysHeaderList={struct.unpack_from('>I', r, 32)[0]} "
          f"(expect 2) BlkHi={struct.unpack_from('>I', r, 132)[0]} "
          f"(expect {bpc - 1}) LoCyl={struct.unpack_from('>I', r, 136)[0]} "
          f"(expect {part_start_cyl}) HiCyl={struct.unpack_from('>I', r, 140)[0]}")

    f = img[2 * SECTOR:3 * SECTOR]
    print(f"FSHD ID: {f[0:4]} (expect b'FSHD')", end="  ")
    print(f"Next=0x{struct.unpack_from('>I', f, 16)[0]:08x} "
          f"Device={f[25:f[24] + 25]} FileName={f[57:f[56] + 57]} "
          f"DosType=0x{struct.unpack_from('>I', f, 88)[0]:08x}")

    p = img[SECTOR:2 * SECTOR]
    print(f"PART ID: {p[0:4]} (expect b'PART')", end="  ")
    print(f"Next=0x{struct.unpack_from('>I', p, 16)[0]:08x} "
          f"Flags=0x{struct.unpack_from('>I', p, 20)[0]:x} "
          f"Name={p[36:68].split(b'\x00')[0]}")

    def env(li):
        return struct.unpack_from('>I', p, 128 + li * 4)[0]
    print(f"Env: size={env(0)} sblk={env(1)} surf={env(3)} spt={env(5)} "
          f"low_cyl={env(9)} high_cyl={env(10)} maxxfer=0x{env(13):x} "
          f"mask=0x{env(14):x} pri={env(15)} dos=0x{env(16):08x}")
    bpt = env(3) * env(5)
    print(f"Partition start block = lowcyl*bpc = {env(9)}*{bpt} = {env(9) * bpt} "
          f"(bootblock at LBA {part_lba}) -> "
          f"{'OK' if env(9) * bpt == part_lba else 'FAIL'}")

    bb2 = img[part_lba * SECTOR:(part_lba + 2) * SECTOR]
    bb_re = carry_wrap_sum(bb2, 2 * SECTOR)
    ok &= bb_re == 0xFFFFFFFF
    print(f"Bootblock checksum: end-around re-sum = 0x{bb_re:08x} "
          f"(expect 0xffffffff) -> {'OK' if bb_re == 0xFFFFFFFF else 'FAIL'}")
    print(f"Boot ID: {bb2[0:4]} (expect b'DOS\\x01') "
          f"code[0:2]={bb2[0x0C:0x0E].hex()} (expect move #$2700,SR = 46fc)")

    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <kernel.elf> <output.hdf> [size_mb]")
        sys.exit(1)
    build(sys.argv[1], sys.argv[2],
          int(sys.argv[3]) if len(sys.argv) > 3 else 16)