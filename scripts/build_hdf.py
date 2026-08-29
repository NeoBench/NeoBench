#!/usr/bin/env python3
"""NeoBench HDF Builder v7 — Fixed RDB/PART/Boot block structures."""

import struct, sys, os

SECTOR = 512

def csum(data, nbytes=None):
    """Amiga RDB/PART/bootblock checksum.

    The ROM requires that the 32-bit sum of ALL longwords in the block,
    INCLUDING the stored checksum, equals 0xFFFFFFFF (-1).  Storing the
    one's complement (~sum) achieves exactly that.
    """
    if nbytes is None: nbytes = len(data)
    s = 0
    for i in range(0, nbytes, 4):
        s = (s + struct.unpack_from('>I', data, i)[0]) & 0xFFFFFFFF
    return (~s) & 0xFFFFFFFF

def w32(d, o, v):
    struct.pack_into('>I', d, o, v & 0xFFFFFFFF)


def make_boot_code(kernel_lba, kernel_blocks, entry_target, load_addr):
    """68k boot code. Must fit in 1012 bytes (1024-byte bootblock minus header)."""
    code = bytearray(504)
    p = 0

    def e16(v):
        nonlocal p; struct.pack_into('>H', code, p, v & 0xFFFF); p += 2
    def e32(v):
        nonlocal p; struct.pack_into('>I', code, p, v & 0xFFFFFFFF); p += 4
    def here(): return p

    # Supervisor + stack
    e16(0x46FC); e16(0x2700)           # MOVE #$2700,SR
    e16(0x41F9); e32(0x00080000)      # LEA $80000,A7

    # --- Print "NB" banner marker ---
    e16(0x41FA); msg_ref = here(); e16(0)
    # Inline serial print loop (reused via BRA)
    lp = here()
    e16(0x1018); e16(0x4A00)          # MOVE.B (A0)+,D0 / TST.B D0
    e16(0x6700); done1 = here(); e16(0)  # BEQ done
    e16(0x0839); e16(0x0006); e32(0x00BFE001)  # BTST #6,$BFE001
    e16(0x67FA)                        # BEQ wait
    e16(0x11C0); e32(0x00BFD100)      # MOVE.B D0,$BFD100
    e16(0x6000)                        # BRA lp
    e16(lp - here())
    struct.pack_into('>H', code, done1, here() - (done1 + 2))
    struct.pack_into('>H', code, msg_ref, here() - (msg_ref + 2))

    # --- Zero IO request at $40000 ---
    e16(0x41F9); e32(0x00040000)      # LEA $40000,A0
    e16(0x703F)                        # MOVEQ #63,D0
    cl = here()
    e16(0x4218); e16(0x51C8); e16(cl - here())  # CLR.L (A0)+ / DBRA

    # --- Fill IO request ---
    e16(0x43F9); e32(0x00040000)      # LEA $40000,A1
    e16(0x31FC); e16(2); e16(20)      # MOVE.W #2,20(A1) CMD_READ
    e16(0x23FC); e32(kernel_blocks * SECTOR); e16(24)  # io_Length
    e16(0x23FC); e32(load_addr); e16(28)          # io_Data
    e16(0x23FC); e32(kernel_lba); e16(32)  # io_Offset

    # --- Open scsi.device ---
    e16(0x43FA); dev_ref = here(); e16(0)  # LEA devname(PC),A1
    e16(0x91C8)                        # SUB.L A0,A0
    e16(0x7000)                        # MOVEQ #0,D0
    e16(0x2CFC); e32(4)               # MOVE.L 4.W,A6
    e16(0x4EAE); e16(0xFDD8)          # JSR -552(A6) OpenDevice

    # --- DoIO ---
    e16(0x43F9); e32(0x00040000)      # LEA $40000,A1
    e16(0x2CFC); e32(4)               # MOVE.L 4.W,A6
    e16(0x4EAE); e16(0xFD90)          # JSR -624(A6) DoIO

    # --- Print "OK" ---
    e16(0x41FA); ok_ref = here(); e16(0)
    lp2 = here()
    e16(0x1018); e16(0x4A00)
    e16(0x6700); done2 = here(); e16(0)
    e16(0x0839); e16(0x0006); e32(0x00BFE001)
    e16(0x67FA)
    e16(0x11C0); e32(0x00BFD100)
    e16(0x6000); e16(lp2 - here())
    struct.pack_into('>H', code, done2, here() - (done2 + 2))
    struct.pack_into('>H', code, ok_ref, here() - (ok_ref + 2))

    # --- Jump to kernel entry ---
    # The kernel file was copied flat to $200000, so the true runtime
    # address of the ELF entry point is computed on the host side
    # (e_entry is a link-time VMA, NOT an offset into the loaded image).
    e16(0x43F9); e32(entry_target)    # LEA $<entry_target>,A1

    # --- Disable interrupts and jump ---
    e16(0x46FC); e16(0x2700)          # MOVE #$2700,SR
    e16(0x4ED1)                        # JMP (A1)

    # ================================================
    # Strings (packed tightly)
    # ================================================
    banner = b"NB:NeoBench v1.0\r\n"
    code[p:p+len(banner)] = banner; p += len(banner)

    struct.pack_into('>H', code, dev_ref, here() - (dev_ref + 2))
    devname = b"scsi.device\x00"
    code[p:p+len(devname)] = devname; p += len(devname)

    okmsg = b"OK\r\n"
    code[p:p+len(okmsg)] = okmsg; p += len(okmsg)

    return code[:p]


def make_part(next_block, dostype, name, lowcyl, highcyl, bootpri,
              total_cyls, heads, spt, bpc):
    """Build a PartitionBlock per devices/hardblocks.h.

    struct PartitionBlock layout:
      0  pb_ID          'PART'
      4  pb_SummedLongs 128
      8  pb_ScsiHost
      12 pb_Next
      16 pb_Flags       bit0 = PBFB_BOOT
      20 pb_Reserved1
      24 pb_DevFlags
      28 pb_DriveName[32]
      60 pb_Reserved2[2]
      68 pb_Environment[]   <- DE_ table lives HERE
         env[0] DE_TABLESIZE
         env[1] DE_SIZEBLOCK   (in longwords: 512B -> 128)
         env[2] DE_SECORG
         env[3] DE_NUMHEADS
         env[4] DE_SECSPERTRACK
         env[5] DE_BLKSTRACK
         env[6] DE_FILESYSTEM  (DOSType!)
         env[7] DE_LOWCYL
         env[8] DE_HIGHCYL
         env[9] DE_NUMBUFFER
         env[10] DE_BUFMEMTYPE
         env[11] DE_MAXTRANSFER
         env[12] DE_MASK
         env[13] DE_BOOTPRI
         env[14] DE_DOSBASE
         env[15] DE_BOOTBLOCKS (=2)
     136 pb_EReserved[]
    """
    p = bytearray(SECTOR)
    w32(p,  0, 0x50415254)             # par_ID = "PART"
    w32(p,  4, 128)                     # par_SummedLongs = 128
                                        # par_ChkSum written last @8
    w32(p, 12, 0x7FFFFFFF)              # par_HostID
    w32(p, 16, next_block)              # par_Next
    w32(p, 20, 0x00000003)              # par_Flags: BOOTABLE | NODOSYNC
    w32(p, 24, 0)                       # par_Reserved1
    w32(p, 28, 0)                       # par_DevFlags
    p[32:32+len(name)+1] = name + b'\x00'   # par_DriveName[32]
                                        # 64..67 par_Reserved2[2]

    env = [0] * 17
    env[0]  = 16                        # DE_TABLESIZE (last used index)
    env[1]  = SECTOR // 4               # DE_SIZEBLOCK in longwords
    env[2]  = 0                         # DE_SECORG
    env[3]  = heads                     # DE_NUMHEADS
    env[4]  = spt                       # DE_SECSPERTRACK
    env[5]  = bpc                       # DE_BLKSTRACK
    env[6]  = dostype                   # DE_FILESYSTEM == DOSType
    env[7]  = lowcyl                    # DE_LOWCYL
    env[8]  = highcyl                   # DE_HIGHCYL
    env[9]  = 30                        # DE_NUMBUFFER
    env[10] = 0                         # DE_BUFMEMTYPE (any)
    env[11] = 0x00200000                # DE_MAXTRANSFER
    env[12] = 0x7FFFFFFE                # DE_MASK
    env[13] = bootpri                   # DE_BOOTPRI
    env[14] = -1                        # DE_DOSBASE (use default)
    env[15] = 2                         # DE_BOOTBLOCKS (1024 bytes)
    env[16] = 0                         # DE_ reserved / terminator pad
    for i, v in enumerate(env):
        w32(p, 68 + i * 4, v)

    w32(p, 8, csum(p))                  # checksum over whole sector
    return p


def build(kernel_path, out_path, size_mb=16):
    with open(kernel_path, 'rb') as f:
        kernel = f.read()

    # ---- Parse ELF32 MSB: find runtime address of e_entry when the
    #      whole file is loaded flat at KERNEL_BASE. ----
    # NeoBench runs the kernel from Zorro III RAM (A4000, 0x20000000);
    # kernel/arch/m68k/linker.ld links with VMA = KERNEL_BASE + offset.
    KERNEL_BASE = 0x20000000
    assert kernel[0:4] == b'\x7fELF' and kernel[4] == 1 and kernel[5] == 2, \
        "expect ELF32 big-endian"
    e_entry   = struct.unpack_from('>I', kernel, 24)[0]
    phoff     = struct.unpack_from('>I', kernel, 28)[0]
    phentsize = struct.unpack_from('>H', kernel, 42)[0]
    phnum     = struct.unpack_from('>H', kernel, 44)[0]
    entry_off = None
    for i in range(phnum):
        ph = phoff + i * phentsize
        p_type, p_offset, p_vaddr = struct.unpack_from('>III', kernel, ph)
        if p_type == 1 and p_vaddr <= e_entry < p_vaddr + max(
                struct.unpack_from('>I', kernel, ph+16)[0], 1):
            entry_off = p_offset + (e_entry - p_vaddr)
            break
    if entry_off is None:
        print("ERROR: could not locate entry point in any PT_LOAD segment")
        sys.exit(1)
    entry_target = KERNEL_BASE + entry_off
    print(f"Kernel ELF: e_entry=0x{e_entry:x} -> load offset 0x{entry_off:x} "
          f"-> jump target 0x{entry_target:08x}")

    total_sec = (size_mb * 1024 * 1024) // SECTOR
    img = bytearray(total_sec * SECTOR)

    heads = 16; spt = 64; bpc = heads * spt
    total_cyls = total_sec // bpc  # 32

    ffs_start_cyl = 1; ffs_end_cyl = total_cyls - 1
    ffs_lba = ffs_start_cyl * bpc

    n_data = (len(kernel) + SECTOR - 1) // SECTOR
    ffs_blocks = (ffs_end_cyl - ffs_start_cyl + 1) * bpc

    # Partition-relative block layout:
    #   0-1       boot block (2 sectors)
    #   2..2+NBMP-1  bitmap blocks (one per 4096 blocks)
    #   REL_HEADER   FFS file header
    #   REL_KERNEL   kernel data (NeoLoader reads this flat)
    #   ...n_data... extension headers, then root block
    # FFS bitmap blocks: 16-byte header, 496 bytes = 3968 bits
    # per block; 8 blocks cover 31744 blocks (31 cyls @ 1024).
    BMP_HDR = 16
    BITS_PER_BMP = (SECTOR - BMP_HDR) * 8
    NBMP = max(1, (ffs_blocks + BITS_PER_BMP - 1) // BITS_PER_BMP)
    if NBMP > 8:
        print(f"ERROR: bitmap needs {NBMP} blocks (FFS allows 8)")
        sys.exit(1)
    REL_HEADER = 2 + NBMP
    REL_KERNEL = REL_HEADER + 1
    kernel_lba = ffs_lba + REL_KERNEL

    print(f"Kernel: {len(kernel)} bytes, {n_data} blocks at LBA {kernel_lba}")
    print(f"Geometry: {total_cyls} cyls, {heads} heads, {spt} spt, {bpc} sec/cyl")
    print(f"FFS:  cyl {ffs_start_cyl}-{ffs_end_cyl} = {ffs_blocks} blocks")

    # ================================================
    # RDB at sector 1 (Rigid Disk Block)
    # Correct Amiga RDSK structure
    # ================================================
    rdb = bytearray(SECTOR)
    w32(rdb,  0, 0x5244534B)           # rdb_ID = "RDSK"
    w32(rdb,  4, 128)                   # rdb_SummedLongs = 128 (full sector)
    w32(rdb, 12, 0x7FFFFFFF)           # rdb_HostID
    w32(rdb, 16, SECTOR)               # rdb_BlockSize = 512
    w32(rdb, 20, 2)                     # rdb_Flags = RDBFF_LAST
    w32(rdb, 24, 0)                     # rdb_BadBlockList = 0
    w32(rdb, 28, 2)                     # rdb_PartitionList = sector 2
    w32(rdb, 32, 0)                     # rdb_FileSysHeaderList
    w32(rdb, 36, 0)                     # rdb_DriveInit
    w32(rdb, 44, 0)                     # rdb_RDBBlocksLo = 0
    w32(rdb, 48, 3)                     # rdb_RDBBlocksHi = 3
    w32(rdb, 52, bpc)                   # rdb_CylSectors = 1024
    w32(rdb, 56, heads)                 # rdb_HeadsPerCyl = 16
    w32(rdb, 76, 0)                     # rdb_LoCylinder = 0
    w32(rdb, 80, total_cyls - 1)        # rdb_HiCylinder = 31
    w32(rdb, 84, bpc)                   # rdb_CylBlocks = 1024
    w32(rdb, 92, 3)                     # rdb_HighRDSKBlock = 3
    w32(rdb, 8, csum(rdb, 512))
    img[SECTOR:2*SECTOR] = rdb

    # ================================================
    # Partition 1: FFS boot (cyl 1-31, whole data area).
    # The kernel (8 MB+) is stored as one contiguous "file";
    # its header block list is chained across extension
    # headers because a single sector can only hold 96
    # block pointers.  NeoLoader boots the kernel directly
    # from kernel_lba, so these structures are a mountable
    # stub for Kickstart/AmigaOS rather than the boot path.
    # ================================================
    p1 = make_part(next_block=0, dostype=0x444F5301, name=b'boot',
                   lowcyl=ffs_start_cyl, highcyl=ffs_end_cyl, bootpri=0,
                   total_cyls=total_cyls, heads=heads, spt=spt, bpc=bpc)
    img[2*SECTOR:3*SECTOR] = p1

    # ================================================
    # FFS Boot block at ffs_lba — TWO sectors (1024 bytes).
    # KS reads DE_BOOTBLOCKS * SizeBlock = 2 * 512 bytes and
    # validates the checksum across the whole 1024-byte block.
    # 'DOS\x01' at offset 0, checksum at offset 4, code from offset 12.
    # ================================================
    bc = make_boot_code(kernel_lba, n_data, entry_target, KERNEL_BASE)
    if len(bc) > 2*SECTOR - 12:
        print(f"ERROR: Boot code {len(bc)} bytes > {2*SECTOR-12} limit")
        sys.exit(1)
    bb = bytearray(2*SECTOR)
    bb[0:4] = b'DOS\x01'
    bb[8:8+len(bc)] = bc
    w32(bb, 4, csum(bb))
    off = ffs_lba * SECTOR
    img[off:off+2*SECTOR] = bb

    # ================================================
    # Layout within the FFS partition (relative blocks):
    #   0-1 boot block, 2..2+NBMP-1 bitmap blocks,
    #   REL_HEADER file header, REL_KERNEL kernel data,
    #   then extension headers and the root block.
    # ================================================
    N_PER_HEADER = (SECTOR - 128) // 4   # 96 block pointers per header
    n_main = min(n_data, N_PER_HEADER)
    n_ext = 0
    if n_data > n_main:
        n_ext = (n_data - n_main + N_PER_HEADER - 1) // N_PER_HEADER
    ext_rel_base = REL_KERNEL + n_data
    root_rel = ext_rel_base + n_ext

    if root_rel >= ffs_blocks:
        print(f"ERROR: kernel too large for FFS partition "
              f"({root_rel + 1} > {ffs_blocks} blocks)")
        sys.exit(1)

    # ================================================
    # FFS Root block
    # ================================================
    root_sector = ffs_lba + root_rel
    rb = bytearray(SECTOR)
    w32(rb,  0, 0x524F4F54)            # rt_ID = "ROOT"
    w32(rb,  4, 128)                    # rt_SummedLongs = 128
    w32(rb, 12, 0)                      # rt_VolumeDate (set to 0)
    w32(rb, 16, 0)                      # rt_Link = 0
    w32(rb, 20, 1)                      # rt_SubDirs = 1 (root itself)
    w32(rb, 24, 0)                      # rt_HashChain = 0 (empty hash)
    # rt_BitMapBlocks[8] at offsets 40..72 -> plural bitmap blocks
    for k in range(min(NBMP, 8)):
        w32(rb, 40 + k * 4, ffs_lba + 2 + k)
    w32(rb, 8, csum(rb, 512))
    off = root_sector * SECTOR
    img[off:off+SECTOR] = rb

    # ================================================
    # FFS Bitmap blocks at ffs_lba+2 .. +2+NBMP-1
    # ================================================
    used_blocks = [0, 1]  # boot blocks
    for k in range(NBMP):
        used_blocks.append(2 + k)      # bitmap blocks
    used_blocks.append(REL_HEADER)     # file header
    used_blocks.append(root_rel)       # root block
    for i in range(n_data):
        used_blocks.append(REL_KERNEL + i)   # kernel data
    for i in range(n_ext):
        used_blocks.append(ext_rel_base + i) # extension headers

    for k in range(NBMP):
        bm = bytearray(SECTOR)
        w32(bm, 0, 0x424D4150)         # bm_ID = "BMAP"
        w32(bm, 4, 128)                 # bm_SummedLongs = 128
        w32(bm, 12, ffs_blocks)         # bm_RegionSize
        base = k * BITS_PER_BMP
        for b in used_blocks:
            if base <= b < base + BITS_PER_BMP and b < ffs_blocks:
                bit = b - base
                bm[BMP_HDR + bit // 8] |= 0x80 >> (bit % 8)
        w32(bm, 8, csum(bm, 512))
        off = (ffs_lba + 2 + k) * SECTOR
        img[off:off+SECTOR] = bm

    # ================================================
    # FFS file header + extension chain.  Each 512-byte
    # header holds 96 data-block pointers; fh_Extension
    # (offset 24) chains to the next header for big files.
    # ================================================
    def write_header(lba_rel, data_start, count, next_lba, byte_size):
        h = bytearray(SECTOR)
        w32(h, 0, 0x46494C45)          # fh_ID = "FILE"
        w32(h, 4, 128)                   # fh_SummedLongs = 128
        w32(h, 12, ffs_lba + lba_rel)    # fh_OwnKey
        w32(h, 20, count)                # fh_DataSize (pointers listed)
        w32(h, 24, next_lba)             # fh_Extension (0 = last)
        w32(h, 28, byte_size)            # fh_ByteSize
        for i in range(count):
            w32(h, 128 + i * 4, ffs_lba + data_start + i)
        w32(h, 8, csum(h, 512))
        img[(ffs_lba + lba_rel) * SECTOR:(ffs_lba + lba_rel + 1) * SECTOR] = h

    first_ext = (ffs_lba + ext_rel_base) if n_ext > 0 else 0
    write_header(REL_HEADER, REL_KERNEL, n_main, first_ext, len(kernel))

    for j in range(n_ext):
        lba_rel = ext_rel_base + j
        data_start = REL_KERNEL + n_main + j * N_PER_HEADER
        count = min(N_PER_HEADER, n_data - (n_main + j * N_PER_HEADER))
        next_lba = (ffs_lba + ext_rel_base + j + 1) if j + 1 < n_ext else 0
        write_header(lba_rel, data_start, count, next_lba, len(kernel))

    # ================================================
    # Kernel data at REL_KERNEL (NeoLoader reads this flat)
    # ================================================
    off = (ffs_lba + REL_KERNEL) * SECTOR
    img[off:off+len(kernel)] = kernel

    # ================================================
    # Write output
    # ================================================
    with open(out_path, 'wb') as f:
        f.write(img)

    print(f"\nHDF: {out_path} ({size_mb} MB)")
    print(f"RDB:       LBA 1 (RDSK)")
    print(f"PART 1:    LBA 2 (FFS cyl {ffs_start_cyl}-{ffs_end_cyl}, {ffs_blocks} blocks)")
    print(f"FFS boot:  LBA {ffs_lba}")
    print(f"Kernel:    LBA {kernel_lba} ({n_data} blocks, "
          f"{n_ext} header extensions)")
    print(f"Root:      LBA {root_sector}")

    # Verify structures
    print("\n=== Verification ===")

    # Verify RDB
    rdb2 = img[SECTOR:2*SECTOR]
    rdb_id = rdb2[0:4]
    rdb_sl = struct.unpack_from('>I', rdb2, 4)[0]
    rdb_csum = struct.unpack_from('>I', rdb2, 8)[0]
    rdb_pl = struct.unpack_from('>I', rdb2, 28)[0]
    rdb_bs = struct.unpack_from('>I', rdb2, 16)[0]
    rdb_hs = struct.unpack_from('>I', rdb2, 56)[0]
    rdb_cs = struct.unpack_from('>I', rdb2, 52)[0]
    total = 0
    for i in range(0, 512, 4):
        total = (total + struct.unpack_from('>I', rdb2, i)[0]) & 0xFFFFFFFF
    print(f"RDB ID: {rdb_id} (expect RDSK)")
    print(f"RDB SummedLongs: {rdb_sl} (expect 128)")
    print(f"RDB Checksum: 0x{rdb_csum:08x} (sum incl. chk=0x{total:08x}, "
          f"expect ffffffff) -> {'OK' if total == 0xFFFFFFFF else 'FAIL'}")
    print(f"RDB PartitionList: {rdb_pl} (expect 2)")
    print(f"RDB BlockSize: {rdb_bs} (expect 512)")
    print(f"RDB CylSectors: {rdb_cs} (expect {bpc})")
    print(f"RDB HeadsPerCyl: {rdb_hs} (expect {heads})")

    # Verify PART 1
    part1 = img[2*SECTOR:3*SECTOR]
    p1_id = part1[0:4]
    p1_next = struct.unpack_from('>I', part1, 16)[0]
    p1_dt = part1[68+6*4:68+6*4+4]          # pb_Environment[DE_FILESYSTEM]
    p1_dn = part1[32:64].split(b'\x00')[0]  # pb_DriveName
    p1_lowcyl = struct.unpack_from('>I', part1, 68+7*4)[0]
    p1_highcyl = struct.unpack_from('>I', part1, 68+8*4)[0]
    total = 0
    for i in range(0, 512, 4):
        total = (total + struct.unpack_from('>I', part1, i)[0]) & 0xFFFFFFFF
    print(f"\nPART1 ID: {p1_id} (expect PART)")
    print(f"PART1 Next: {p1_next} (expect 0)")
    print(f"PART1 DOSType @env[6]: {p1_dt} (expect DOS\\x01)")
    print(f"PART1 DriveName: {p1_dn} (expect boot)")
    print(f"PART1 Cylinders: {p1_lowcyl}-{p1_highcyl}")
    print(f"PART1 Checksum verify: 0x{total:08x} "
          f"(expect ffffffff) -> {'OK' if total == 0xFFFFFFFF else 'FAIL'}")

    # Verify Boot block (1024 bytes)
    bb2 = img[ffs_lba*SECTOR:(ffs_lba+2)*SECTOR]
    bb_hdr = bb2[0:4]
    bb_csum = struct.unpack_from('>I', bb2, 4)[0]
    total = 0
    for i in range(0, 1024, 4):
        total = (total + struct.unpack_from('>I', bb2, i)[0]) & 0xFFFFFFFF
    print(f"\nBoot ID: {bb_hdr} (expect DOS\\x01)")
    print(f"Boot Checksum: 0x{bb_csum:08x} (sum over 1024B incl. chk="
          f"0x{total:08x}, expect ffffffff) -> "
          f"{'OK' if total == 0xFFFFFFFF else 'FAIL'}")
    print(f"Boot first code byte: 0x{bb2[8]:02x} (expect 0x46 = MOVE SR)")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <kernel.elf> <output.hdf> [size_mb]")
        sys.exit(1)
    build(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 16)
