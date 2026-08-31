#!/usr/bin/env python3
"""Build a deterministic NBFS v1 fixture image for the host handler tests.

Encodings match the kernel (kernel/include/nbfs.h, kernel/fs/nbfs/nbfs.c):

  superblock block 1 (offsets in bytes):
    magic u32 @0, vmaj u16 @4, vmin u16 @6, bs u32 @8, flags u32 @12,
    total_blocks u64 @16, free_blocks u64 @24,
    total_inodes u64 @32, free_inodes u64 @40,
    root_inode u64 @48, journal_start u64 @56, journal_blocks u64 @64,
    block_bitmap_start u64 @72, inode_bitmap_start u64 @80,
    inode_table_start u64 @88, data_start u64 @96,
    volume_name char[64] @104, crc32 u32 @168, reserved[128] @172.

  inode (248 B, slot index = inode_number - 1):
    inode_number u64 @0, mode u16 @8, links u16 @10, uid u32 @12,
    gid u32 @16, size u64 @20, created/modified/accessed u64 @28/36/44,
    extents[12] @52 (start u64, count u32, flags u32), crc32 u32 @244.

  dirent (in dir data): inode u64 @0, record_length u16 @8,
    name_length u8 @10, type u8 @11 (1=file 2=dir), name at +12.
    record_length = 12 + name_length.  Zero record_length marks the end.
"""

import struct
import sys

BLOCK = 4096
MODE_DIR = 0x4000
MODE_FILE = 0x8000
ENTRY_DIR = 2
ENTRY_FILE = 1
INODE_SIZE = 248


def le16(v):
    return struct.pack("<H", v)


def le32(v):
    return struct.pack("<I", v)


def le64(v):
    return struct.pack("<Q", v)


def make_inode(number, mode, size, extents, links=1):
    b = bytearray(INODE_SIZE)
    b[0:8] = le64(number)
    b[8:10] = le16(mode)
    b[10:12] = le16(links)
    b[12:16] = le32(0)
    b[16:20] = le32(0)
    b[20:28] = le64(size)
    b[28:36] = le64(0)
    b[36:44] = le64(0)
    b[44:52] = le64(0)
    for i, (start, count, flags) in enumerate(extents[:12]):
        off = 52 + i * 16
        b[off:off + 8] = le64(start)
        b[off + 8:off + 12] = le32(count)
        b[off + 12:off + 16] = le32(flags)
    return bytes(b)


def make_dirent(ino, name, etype):
    nb = name.encode("latin-1")
    rec = 12 + len(nb)
    b = bytearray(rec)
    b[0:8] = le64(ino)
    b[8:10] = le16(rec)
    b[10] = len(nb)
    b[11] = etype
    b[12:12 + len(nb)] = nb
    return bytes(b)


def dir_block(entries):
    b = bytearray(BLOCK)
    off = 0
    for d in entries:
        b[off:off + len(d)] = d
        off += len(d)
    return bytes(b)


class Fs:
    def __init__(self, img):
        self.img = img
        self.blocks = len(img) // BLOCK
        self.next_block = 325
        self.next_inode = 2
        self.inodes = {}
        self.dirs = {}

    def alloc_block(self):
        b = self.next_block
        self.next_block += 1
        return b

    def add_inode(self, mode, size, extents, links=1):
        n = self.next_inode
        self.next_inode += 1
        self.inodes[n] = make_inode(n, mode, size, extents, links)
        return n

    def add_file(self, parent, parent_name, name, data):
        blocks = (len(data) + BLOCK - 1) // BLOCK
        starts = [self.alloc_block() for _ in range(blocks)]
        ino = self.add_inode(
            MODE_FILE, len(data),
            [(s, 1, 0) for s in starts])
        for i, s in enumerate(starts):
            chunk = data[i * BLOCK:(i + 1) * BLOCK]
            self.img[s * BLOCK:s * BLOCK + len(chunk)] = chunk
        return ino

    def add_dir(self, parent_list):
        b = self.alloc_block()
        ino = self.add_inode(MODE_DIR, BLOCK, [(b, 1, 0)])
        return ino, b

    def finish(self):
        for ino, entries in self.dirs.items():
            start = struct.unpack_from("<Q", self.inodes[ino], 52)[0]
            self.img[start * BLOCK:(start + 1) * BLOCK] = dir_block(entries)

        for number, raw in self.inodes.items():
            assert number >= 1
            index = number - 1
            block = 4 + index // 16
            off = (index % 16) * INODE_SIZE
            base = block * BLOCK + off
            self.img[base:base + INODE_SIZE] = raw

        num_used_inodes = len(self.inodes)
        used_blocks = self.next_block
        free_blocks = self.blocks - used_blocks

        sb = bytearray(self.img[BLOCK:2 * BLOCK])
        sb[24:32] = le64(free_blocks)
        sb[40:48] = le64(1024 - num_used_inodes)
        self.img[BLOCK:2 * BLOCK] = sb


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/fixture/fixture.nbfs"
    total_blocks = 2048

    img = bytearray(total_blocks * BLOCK)
    img[0:64] = b"NEBOOT" + b"\0" * 58

    # superblock (block 1)
    sb = bytearray(BLOCK)
    sb[0:4] = b"NBFS"
    sb[4:6] = le16(1)
    sb[6:8] = le16(0)
    sb[8:12] = le32(BLOCK)
    sb[12:16] = le32(0)
    sb[16:24] = le64(total_blocks)
    sb[24:32] = le64(0)      # patched in finish()
    sb[32:40] = le64(1024)
    sb[40:48] = le64(0)      # patched in finish()
    sb[48:56] = le64(1)
    sb[56:64] = le64(68)
    sb[64:72] = le64(0)
    sb[72:80] = le64(2)
    sb[80:88] = le64(3)
    sb[88:96] = le64(4)
    sb[96:104] = le64(324)
    sb[104:110] = b"FIXTURE"
    img[BLOCK:2 * BLOCK] = sb

    fs = Fs(img)

    contents = {
        "motd": b"Greetings from NBFS.\n",
        "version": b"vT-1\n",
        "readme.txt": b"Docs for the handler test.\n" * 5,
        "README.txt": b"NeoBench read-only test fixture\n" * 30,
        "flag.txt": b"NBFS-HANDLER-FLAG\n",
    }

    # etc/ (dir inode 2)
    etc_ino, etc_block = fs.add_dir(None)
    fs.next_block = etc_block + 1  # keep monotonic anyway
    fs.dirs[etc_ino] = [
        make_dirent(etc_ino, ".", ENTRY_DIR),
        make_dirent(1, "..", ENTRY_DIR),
        make_dirent(fs.add_file(etc_ino, "etc", "motd", contents["motd"]),
                    "motd", ENTRY_FILE),
        make_dirent(fs.add_file(etc_ino, "etc", "version",
                               contents["version"]),
                    "version", ENTRY_FILE),
    ]

    # docs/ (dir inode 3)
    docs_ino, _ = fs.add_dir(None)
    fs.dirs[docs_ino] = [
        make_dirent(docs_ino, ".", ENTRY_DIR),
        make_dirent(1, "..", ENTRY_DIR),
        make_dirent(fs.add_file(docs_ino, "docs", "readme.txt",
                               contents["readme.txt"]),
                    "readme.txt", ENTRY_FILE),
    ]

    # root (inode 1)
    root_block = fs.alloc_block()
    fs.inodes[1] = make_inode(1, MODE_DIR, BLOCK, [(root_block, 1, 0)],
                              links=3)
    fs.dirs[1] = [
        make_dirent(1, ".", ENTRY_DIR),
        make_dirent(1, "..", ENTRY_DIR),
        make_dirent(etc_ino, "etc", ENTRY_DIR),
        make_dirent(docs_ino, "docs", ENTRY_DIR),
        make_dirent(fs.add_file(1, "/", "README.txt", contents["README.txt"]),
                    "README.txt", ENTRY_FILE),
        make_dirent(fs.add_file(1, "/", "flag.txt", contents["flag.txt"]),
                    "flag.txt", ENTRY_FILE),
    ]

    fs.finish()

    with open(out, "wb") as f:
        f.write(img)
    print("wrote %s (%d bytes)" % (out, len(img)))


if __name__ == "__main__":
    main()