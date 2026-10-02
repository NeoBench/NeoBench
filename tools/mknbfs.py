#!/usr/bin/env python3
"""Build a populated NBFS volume from a directory tree.

    python3 tools/mknbfs.py system images/nbfs.img

This is the on-disk half of the installer: the image it writes is the
payload the ROM streams from the install disc onto a hard disk, and it
is a real NBFS volume -- the same layout tools/nbfs/mkfs.nbfs creates
(boot block "NBBOOT", superblock at block 1, little-endian structures
of include/nbfs/nbfs.h), only with the system tree filled in instead of
a bare root directory.

Every structure is written exactly the way mkfs.nbfs writes it, byte
for byte, because nbfs-info and libnbfs read that layout and not a
document: the drafts in docs/nbfs/ describe an older design and are not
what either tool implements.  The differences from mkfs are the ones a
populated volume needs -- file inodes, subdirectory blocks, counts that
add up -- and mkfs's own quirks (no CRC anywhere, timestamps of zero,
one link per directory) are kept rather than corrected, because being
readable by the tools that exist beats being tidy in isolation.

The image is verified by re-opening it and walking it with a separate
parser before the tool reports success, so a truncated write or a
directory that lost an entry fails here rather than on the machine.
"""

import os
import struct
import sys

BLOCK = 4096
TOTAL_BLOCKS_DEFAULT = 1024     # 4 MB: the fixed layout, smaller volume
INODES = 1024                   # the inode table holds exactly this many
DATA_START = 324                # boot, superblock, bitmaps, table, journal

# include/nbfs/nbfs.h, little-endian and packed, as mkfs writes them.
SUPER_FMT = "<IHHII" + "Q" * 11 + "64sI128s"
INODE_FMT = "<QHHII4Q" + "16s" * 12 + "I"
DIRENT_FMT = "<QHBB252s"

SUPER_SIZE = struct.calcsize(SUPER_FMT)      # 300
INODE_SIZE = struct.calcsize(INODE_FMT)      # 248
DIRENT_SIZE = struct.calcsize(DIRENT_FMT)    # 264

MAGIC = 0x5346424E           # "NBFS" as a little-endian u32
S_IFDIR = 0x4000             # mkfs's mode for the root directory
S_IFREG = 0x81A4             # regular file, 0644: mkfs never wrote one
TYPE_DIR = 2                 # mkfs's dirent type for "." and ".."
TYPE_FILE = 1


def collect(root):
    """Nodes as (name, path, parent, isdir, bytes) in directory-first order.

    The same walk tools/mkpfs.py uses: sorted entries, a directory's
    node placed before its children, so inode numbers come out in a
    stable order that matches the tree the ROM packs.
    """
    nodes = [("/", "", None, True, b"")]

    def walk(rel, parent):
        base = os.path.join(root, rel) if rel else root
        try:
            entries = sorted(os.listdir(base))
        except OSError:
            return
        for name in entries:
            full = os.path.join(base, name)
            path = "{}/{}".format(rel, name) if rel else name
            if os.path.isdir(full):
                idx = len(nodes)
                nodes.append((name, path, parent, True, b""))
                walk(path, idx)
            elif os.path.isfile(full):
                with open(full, "rb") as fh:
                    nodes.append((name, path, parent, False, fh.read()))

    walk("", 0)
    return nodes


def dirent(inode, name, typ):
    raw = name.encode("latin-1")
    if len(raw) > 251:
        raise SystemExit("mknbfs: name too long: " + name)
    return struct.pack(DIRENT_FMT, inode, DIRENT_SIZE, len(raw), typ,
                       raw.ljust(252, b"\0"))


def children_of(nodes, i):
    """The node indices of directory i's children, in node order."""
    return [j for j, node in enumerate(nodes) if node[2] == i]


def dir_bytes(nodes, i):
    """Directory i's block contents: ".", "..", then its children.

    ".." points at the parent (or at itself for the root), the same
    fixed 264-byte records mkfs writes, so nbfs-info can index them.
    """
    parent = nodes[i][2]
    out = [dirent(i + 1, ".", TYPE_DIR),
           dirent((parent + 1) if parent is not None else (i + 1),
                  "..", TYPE_DIR)]
    for j in children_of(nodes, i):
        node = nodes[j]
        out.append(dirent(j + 1, node[0],
                          TYPE_DIR if node[3] else TYPE_FILE))
    return b"".join(out)


def dir_blocks(nodes, i):
    return (len(dir_bytes(nodes, i)) + BLOCK - 1) // BLOCK


def build(nodes, total_blocks, volume):
    """The volume as bytes, laid out the way mkfs.nbfs lays out its own."""
    if len(nodes) + 1 > INODES:
        raise SystemExit("mknbfs: more nodes than the inode table holds")

    # One contiguous extent per node, allocated in node order from the
    # first data block: an extent is a run, and nothing here fragments.
    alloc = DATA_START
    layout = {}                    # node index -> (start block, count)

    for i, node in enumerate(nodes):
        if node[3]:
            need = dir_blocks(nodes, i)
        else:
            need = (len(node[4]) + BLOCK - 1) // BLOCK
        if need:
            if alloc + need > total_blocks:
                raise SystemExit("mknbfs: volume full: raise --blocks")
            layout[i] = (alloc, need)
            alloc += need

    image = bytearray(total_blocks * BLOCK)

    # Boot block: mkfs writes its signature and nothing else.
    image[0:6] = b"NBBOOT"

    used_inodes = len(nodes)
    used_data = alloc - DATA_START
    sb = struct.pack(
        SUPER_FMT,
        MAGIC, 1, 0, BLOCK, 0,
        total_blocks, total_blocks - DATA_START - used_data,
        INODES, INODES - used_inodes,
        1,                                  # root_inode
        68, 256,                            # journal start, block count
        2, 3, 4, DATA_START,                # bitmaps, table, data start
        volume.encode("latin-1")[:63].ljust(64, b"\0"),
        0,                                  # mkfs leaves crc32 at zero
        b"\0" * 128)
    assert len(sb) == SUPER_SIZE
    image[BLOCK:BLOCK + SUPER_SIZE] = sb

    # Block bitmap: everything before the data area is reserved, plus
    # every block handed out -- the same bits mkfs sets.
    bitmap = bytearray(BLOCK)
    for i in range(alloc):
        bitmap[i // 8] |= 1 << (i % 8)
    image[BLOCK * 2:BLOCK * 3] = bitmap

    # Inode bitmap: bit i is inode i, inode 0 reserved.  mkfs forgot to
    # mark the root; marking every used inode keeps this bitmap and
    # free_inodes telling the same story.
    ibm = bytearray(BLOCK)
    ibm[0] |= 1
    for n in range(1, used_inodes + 1):
        ibm[n // 8] |= 1 << (n % 8)
    image[BLOCK * 3:BLOCK * 4] = ibm

    for i, node in enumerate(nodes):
        start, nblocks = layout.get(i, (0, 0))
        ino = struct.pack(
            INODE_FMT,
            i + 1,                          # inode numbers follow nodes
            S_IFDIR if node[3] else S_IFREG,
            1,                              # links, as mkfs leaves them
            0, 0,                           # uid, gid
            nblocks * BLOCK if node[3] else len(node[4]),
            0, 0, 0,                        # timestamps, as mkfs leaves
            *([struct.pack("<QII", start, nblocks, 0)] +
              [b"\0" * 16] * 11),
            0)                              # crc32, as mkfs leaves it
        assert len(ino) == INODE_SIZE
        off = BLOCK * 4 + i * INODE_SIZE    # inode n sits at n - 1 slots
        image[off:off + INODE_SIZE] = ino

        blob = dir_bytes(nodes, i) if node[3] else node[4]
        image[start * BLOCK:start * BLOCK + len(blob)] = blob

    return bytes(image), alloc


def read_inode(image, n):
    """One inode unpacked, with its extents as (start, count, flags)."""
    off = BLOCK * 4 + (n - 1) * INODE_SIZE
    f = struct.unpack_from(INODE_FMT, image, off)
    extents = [struct.unpack("<QII", f[9 + e]) for e in range(12)]
    return {"mode": f[1], "size": f[5], "extents": extents}


def dir_records(image, start, size):
    """A directory's entries from its data stream.

    The stream is read as one continuous run of fixed records rather
    than block by block: with 264-byte records a block holds fifteen
    and the sixteenth crosses the boundary, and a reader that stopped
    at each edge would lose it.
    """
    stream = image[start * BLOCK:start * BLOCK + size]
    out, pos = [], 0
    while pos + DIRENT_SIZE <= len(stream):
        ino, rlen, nlen, typ, name = struct.unpack_from(DIRENT_FMT,
                                                        stream, pos)
        if ino:
            out.append((ino, typ, name[:nlen].decode("latin-1")))
        pos += rlen if rlen >= DIRENT_SIZE else DIRENT_SIZE
    return out


def verify(image, nodes, total_blocks, volume, alloc):
    """Re-read the image with a separate parser and walk the whole tree.

    Deliberately shares no code with build(): it unpacks the superblock
    from the bytes, follows root -> children by name, checks inode
    numbers and types, and compares every file's body, so a mistake in
    the writer is not also a mistake in the checker.
    """
    (magic, major, minor, bsize, flags, tot, free, tinodes, finodes,
     root, jstart, jblocks, bbm, ibmt, itab, dstart, vname, crc,
     _res) = struct.unpack_from(SUPER_FMT, image, BLOCK)

    def bad(what):
        raise SystemExit("mknbfs: verify: " + what)

    if magic != MAGIC or bsize != BLOCK:
        bad("superblock magic or block size")
    if major != 1:
        bad("version")
    if vname.rstrip(b"\0").decode("latin-1") != volume:
        bad("volume name")
    if root != 1:
        bad("root inode")
    if tot != total_blocks:
        bad("total blocks")
    if free != total_blocks - (alloc - DATA_START) - DATA_START:
        bad("free_blocks does not add up")
    if finodes != INODES - len(nodes):
        bad("free_inodes does not add up")

    # Bitmap: reserved blocks and allocated ones are marked, and the
    # bits the allocation handed out are the only data-area bits set.
    bitmap = image[BLOCK * 2:BLOCK * 3]
    for i in range(DATA_START):
        if not bitmap[i // 8] & (1 << (i % 8)):
            bad("reserved block {} unmarked".format(i))
    for i in range(DATA_START, total_blocks):
        if bitmap[i // 8] & (1 << (i % 8)) and i >= alloc:
            bad("block {} marked but never allocated".format(i))

    def check_dir(i, prefix):
        ino = read_inode(image, i + 1)
        if ino["mode"] != S_IFDIR:
            bad(prefix + ": not a directory")
        start, nblocks, _f = ino["extents"][0]
        if ino["size"] != nblocks * BLOCK:
            bad(prefix + ": directory size")
        recs = dir_records(image, start, ino["size"])
        by_name = {name: (num, typ) for num, typ, name in recs}
        parent = nodes[i][2]
        want_dot = (i + 1, TYPE_DIR)
        if by_name.get(".") != want_dot:
            bad(prefix + ": . entry")
        if by_name.get("..") != (((parent + 1) if parent is not None
                                  else (i + 1)), TYPE_DIR):
            bad(prefix + ": .. entry")
        for j in children_of(nodes, i):
            node = nodes[j]
            if node[0] not in by_name:
                bad("missing " + prefix + node[0])
            num, typ = by_name[node[0]]
            if num != j + 1:
                bad(prefix + node[0] + ": inode {} not {}".format(num,
                                                                  j + 1))
            want = TYPE_DIR if node[3] else TYPE_FILE
            if typ != want:
                bad(prefix + node[0] + ": type")
            if node[3]:
                check_dir(j, prefix + node[0] + "/")
            else:
                fin = read_inode(image, j + 1)
                if fin["mode"] != S_IFREG or fin["size"] != len(node[4]):
                    bad(prefix + node[0] + ": inode mode or size")
                fs, fb, _ff = fin["extents"][0]
                body = image[fs * BLOCK:fs * BLOCK + fin["size"]]
                if body != node[4]:
                    bad(prefix + node[0] + ": content")

    check_dir(0, "")


def main(argv):
    args = argv[1:]
    volume = "NeoBench"
    total = TOTAL_BLOCKS_DEFAULT
    rest, i = [], 0
    while i < len(args):
        if args[i] == "--volume" and i + 1 < len(args):
            volume = args[i + 1]
            i += 2
        elif args[i] == "--blocks" and i + 1 < len(args):
            total = int(args[i + 1])
            i += 2
        else:
            rest.append(args[i])
            i += 1
    if len(rest) != 2:
        raise SystemExit("usage: mknbfs.py <tree> <image> "
                         "[--volume NAME] [--blocks N]")
    tree, out = rest

    nodes = collect(tree)
    image, alloc = build(nodes, total, volume)
    verify(image, nodes, total, volume, alloc)

    with open(out, "wb") as fh:
        fh.write(image)

    ndirs = sum(1 for n in nodes if n[3])
    nfiles = len(nodes) - ndirs
    print("mknbfs: {} ({} dirs, {} files, {} blocks x {} bytes)".format(
        out, ndirs, nfiles, total, BLOCK))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
