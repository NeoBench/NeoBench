#!/usr/bin/env python3
"""Build a NeoBench installer ISO.

    python3 tools/mkiso.py images/iso-root images/NeoBench-0.1.7.iso

An ISO 9660 volume with NeoBench's installer boot block in the system
area -- sector 0 carries the signature "NBISO", and that signature is
what makes the disc an installer disc: on boot NeoBench's ATAPI driver
reads it, and seeing it mounts the ISO 9660 volume at sector 16 and
runs the installation from the payload the volume carries.

What the disc is not is an Amiga boot CD.  A desktop Kickstart boots
from floppies and hard disks only -- the Early Startup Control lists
exactly those two -- and CD boot on the Amiga exists only behind the
CDTV and CD32 ROMs and Commodore's trademark files.  So the "boot" in
"bootable installer" is NeoBench's own: the ROM is the boot medium,
the disc carries the installer, and sector 0 is where the two meet.

The volume is plain ISO 9660 level 1 -- 8.3 names, upper case, ";1"
versions, both-endian fields, L and M path tables, records padded so
none crosses a sector -- so any ISO 9660 reader that mounts it
(AmigaOS's handler included) sees a well-formed disc.  Dates are fixed
rather than taken from the clock so building the same tree twice
produces the same disc, which is what lets a test compare the two.
"""

import os
import struct
import sys

SECTOR = 2048
BOOT_SIG = b"NBISO"
BOOT_VERSION = 1
# 2026-01-01 00:00:00 UTC, fixed for reproducible images.
STAMP = (126, 1, 1, 0, 0, 0, 0)
STAMP_TEXT = b"2026010100000000" + b"\0"      # 17 bytes, GMT
EXPIRATION_TEXT = b"0" * 16 + b"\0"

VALID = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def iso_name(name, isdir):
    """An 8.3 identifier: upper case, invalid characters folded to _.

    Files gain the ";1" version every level 1 disc carries; a
    directory never does.  Names longer than the level 1 limits are
    cut, and the caller checks that no two names cut to the same
    thing, because a silent collision is a file nobody can find.
    """
    base = name.upper()
    if isdir:
        ident = "".join(c if c in VALID else "_" for c in base)[:8]
    else:
        stem, dot, ext = base.rpartition(".")
        if not dot:
            stem, ext = base, ""
        stem = "".join(c if c in VALID else "_" for c in stem)[:8]
        ext = "".join(c if c in VALID else "_" for c in ext)[:3]
        ident = stem + ("." + ext if ext else "") + ";1"
    if not ident:
        raise SystemExit("mkiso: unusable name: " + name)
    return ident


def collect(root):
    """The tree as (name, parent, isdir, data) in directory-first order."""
    nodes = [(None, None, True, None)]       # the root itself

    def walk(rel, parent):
        base = os.path.join(root, rel) if rel else root
        for name in sorted(os.listdir(base)):
            full = os.path.join(base, name)
            if os.path.isdir(full):
                idx = len(nodes)
                nodes.append((name, parent, True, None))
                walk("{}/{}".format(rel, name) if rel else name, idx)
            elif os.path.isfile(full):
                with open(full, "rb") as fh:
                    nodes.append((name, parent, False, fh.read()))

    walk("", 0)
    return nodes


def rec_len(ident):
    """The assembled width of one record: 33 bytes plus identifier
    plus an even-out pad byte -- fixed whatever the extents are, which
    is what lets a directory's length be known before its offsets."""
    n = len(ident)
    return 33 + n + ((33 + n) & 1)


def dir_record(ident, flags, extent, length, name_bytes=None):
    """One ISO 9660 directory record: 33 bytes plus identifier plus pad.

    "." is the single byte 0x00 and ".." the single byte 0x01 -- not
    the characters a Unix directory would use -- and the record length
    is made even with a pad byte, as the standard asks.
    """
    if name_bytes is None:
        name_bytes = ident.encode("ascii")
    body = 33 + len(name_bytes)
    pad = body & 1
    reclen = body + pad               # the length byte counts the pad
    out = bytes([reclen, 0])
    out += both32(extent)
    out += both32(length)
    out += bytes(STAMP)
    out += bytes([flags, 0, 0])
    out += both16(1)                        # volume sequence number
    out += bytes([len(name_bytes)])
    out += name_bytes
    if pad:
        out += b"\0"
    return out


def assemble(records):
    """Directory records as a sector-aligned stream.

    A record may not straddle a sector: when the next record would
    cross the edge, the rest of the sector is zeroed and it starts
    afresh on the next one, which is the rule a reader counts on.
    """
    out = b""
    for rec in records:
        edge = (len(out) // SECTOR + 1) * SECTOR
        if len(out) % SECTOR and len(out) + len(rec) > edge:
            out += b"\0" * (edge - len(out))
        out += rec
    return out


def dir_records(nodes, i, dir_off, file_off, dir_len, file_len):
    """Directory i's records: ".", "..", then its children in order."""
    own = dir_len[i]
    parent = nodes[i][1]
    p = i if parent is None else parent
    out = [dir_record("", 2, dir_off[i], own, name_bytes=b"\0"),
           dir_record("", 2, dir_off[p], dir_len[p], name_bytes=b"\1")]
    for j, node in enumerate(nodes):
        if node[1] != i:
            continue
        ident = iso_name(node[0], node[2])
        if node[2]:
            out.append(dir_record(ident, 2, dir_off[j], dir_len[j]))
        else:
            out.append(dir_record(ident, 0, file_off[j], file_len[j]))
    return out


def path_table(dirs, big_endian):
    """Path table: one entry per directory, parents before children.

    The record leads with the identifier length and the extent's
    attribute length, then the extent and the parent number -- not the
    extent first, which is the directory record's order and a trap
    worth the comment: the two structures look alike and are not.
    """
    fmt = ">I" if big_endian else "<I"
    fmt16 = ">H" if big_endian else "<H"
    out = b""
    for num, parent, ident, extent in dirs:
        name = b"\0" if num == 1 else ident.encode("ascii")
        out += bytes([len(name), 0])              # LEN_DI, xattr length
        out += struct.pack(fmt, extent)
        out += struct.pack(fmt16, parent)
        out += name
        if len(name) % 2:                         # even total, as spec'd
            out += b"\0"
    return out


def build(nodes, volume, out_path):
    n_dirs = sum(1 for n in nodes if n[2])
    n_files = len(nodes) - n_dirs

    # Directory lengths from record sizes alone -- widths do not depend
    # on the extents that will go into the records -- so a placeholder
    # pass gives the exact assembled length, hence the sector count.
    dummy = {k: 0 for k in range(len(nodes))}

    dir_len = {}
    for i, node in enumerate(nodes):
        if not node[2]:
            continue
        recs = dir_records(nodes, i, dummy, dummy, dummy, dummy)
        dir_len[i] = ((len(assemble(recs)) + SECTOR - 1) // SECTOR) \
            * SECTOR

    pvd_sector = 16
    term_sector = 17
    lt_sector = 18
    mt_sector = 19
    dir_start = 20

    dir_off = {}
    sector = dir_start
    for i in range(len(nodes)):
        if nodes[i][2]:
            dir_off[i] = sector
            sector += dir_len[i] // SECTOR
    file_off = {}
    file_len = {}
    for i, node in enumerate(nodes):
        if node[2]:
            continue
        file_off[i] = sector
        file_len[i] = len(node[3])
        sector += (len(node[3]) + SECTOR - 1) // SECTOR
    total_sectors = sector

    # With real offsets in hand, the directory sectors themselves.
    dir_data = {}
    for i, node in enumerate(nodes):
        if node[2]:
            dir_data[i] = assemble(
                dir_records(nodes, i, dir_off, file_off, dir_len,
                            file_len))
            if len(dir_data[i]) > dir_len[i]:
                raise SystemExit("mkiso: directory grew past its "
                                 "sectors: " + str(node[0]))

    # Path table entries: (number, parent number, ident, lba).
    table = []
    for i, node in enumerate(nodes):
        if not node[2]:
            continue
        parent = node[1] if node[1] is not None else 0
        table.append((i + 1, parent + 1, "" if i == 0
                      else node_ident(nodes, i), dir_off[i]))
    lt = path_table(table, big_endian=False)
    mt = path_table(table, big_endian=True)
    if len(lt) > SECTOR or len(mt) > SECTOR:
        raise SystemExit("mkiso: too many directories for one path table")

    root_rec = dir_record("", 2, dir_off[0], dir_len[0],
                          name_bytes=b"\0")
    assert len(root_rec) == 34

    image = bytearray(total_sectors * SECTOR)

    # System area: NeoBench's installer boot block at sector 0.
    boot = bytearray(SECTOR)
    boot[0:5] = BOOT_SIG
    boot[5] = BOOT_VERSION
    vol = volume.encode("ascii")[:32]
    boot[8:8 + len(vol)] = vol
    image[0:SECTOR] = boot

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"NEOBENCH".ljust(32)                # system identifier
    pvd[40:72] = volume.encode("ascii")[:32].ljust(32)
    pvd[80:88] = both32(total_sectors)               # volume space size
    pvd[120:124] = both16(1)                         # volume set size
    pvd[124:128] = both16(1)                         # volume sequence
    pvd[128:132] = both16(SECTOR)                    # logical block size
    pvd[132:140] = both32(len(lt))                   # path table size
    pvd[140:144] = struct.pack("<I", lt_sector)      # L table
    pvd[144:148] = struct.pack("<I", 0)              # optional L
    pvd[148:152] = struct.pack(">I", mt_sector)      # M table
    pvd[152:156] = struct.pack(">I", 0)              # optional M
    pvd[156:190] = root_rec
    pvd[190:318] = b"NEOBENCH INSTALLER".ljust(128)  # volume set id
    pvd[318:446] = b"NEOBENCH".ljust(128)            # publisher id
    pvd[446:574] = b"NEOBENCH".ljust(128)            # preparer id
    pvd[574:702] = b"MKISO".ljust(128)               # application id
    pvd[813:830] = STAMP_TEXT                        # creation date
    pvd[830:847] = STAMP_TEXT                        # modification date
    pvd[847:864] = EXPIRATION_TEXT                   # no expiration
    pvd[864:881] = STAMP_TEXT                        # effective date
    pvd[881] = 1                                     # structure version
    image[pvd_sector * SECTOR:(pvd_sector + 1) * SECTOR] = pvd

    term = bytearray(SECTOR)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1
    image[term_sector * SECTOR:(term_sector + 1) * SECTOR] = term

    image[lt_sector * SECTOR:lt_sector * SECTOR + len(lt)] = lt
    image[mt_sector * SECTOR:mt_sector * SECTOR + len(mt)] = mt

    for i in dir_data:
        off = dir_off[i] * SECTOR
        image[off:off + len(dir_data[i])] = dir_data[i]
    for i, node in enumerate(nodes):
        if node[2]:
            continue
        off = file_off[i] * SECTOR
        image[off:off + len(node[3])] = node[3]

    with open(out_path, "wb") as fh:
        fh.write(image)

    print("mkiso: {} ({} files, {} dirs, {} sectors, volume \"{}\")"
          .format(out_path, n_files, n_dirs, total_sectors, volume))


def node_ident(nodes, i):
    """The ISO identifier for one node."""
    return iso_name(nodes[i][0], nodes[i][2])


def verify(path):
    """Read the disc back: signature, PVD, root listing.

    A second pass in the other direction, so the writer cannot be the
    only thing that understands what it wrote.
    """
    with open(path, "rb") as fh:
        data = fh.read()

    def bad(what):
        raise SystemExit("mkiso: verify: " + what)

    if data[0:5] != BOOT_SIG or data[5] != BOOT_VERSION:
        bad("boot block at sector 0")
    pvd = data[16 * SECTOR:17 * SECTOR]
    if pvd[0] != 1 or pvd[1:6] != b"CD001" or pvd[6] != 1:
        bad("primary volume descriptor")
    (space,) = struct.unpack_from("<I", pvd, 80)
    if space * SECTOR != len(data):
        bad("volume space size does not match the image")
    (blocksize,) = struct.unpack_from("<H", pvd, 128)
    if blocksize != SECTOR:
        bad("logical block size")
    (lt_size,) = struct.unpack_from("<I", pvd, 132)
    (lt_lba,) = struct.unpack_from("<I", pvd, 140)
    if lt_lba * SECTOR + lt_size > len(data):
        bad("path table past end of image")
    extent = struct.unpack_from("<I", pvd, 158)[0]
    length = struct.unpack_from("<I", pvd, 166)[0]
    recs = read_dir(data, extent, length)
    return sorted(name for _lba, _flags, name in recs)


def read_dir(data, extent, length):
    """The records of one directory, stepping over sector padding."""
    out, pos = [], extent * SECTOR
    end = pos + length
    while pos + 33 <= end:
        reclen = data[pos]
        if reclen == 0:
            pos = (pos // SECTOR + 1) * SECTOR      # zero fill to the edge
            continue
        lba = struct.unpack_from("<I", data, pos + 2)[0]
        flags = data[pos + 25]
        nlen = data[pos + 32]
        name = data[pos + 33:pos + 33 + nlen]
        if name not in (b"\0", b"\1"):
            out.append((lba, flags, name.decode("ascii")))
        pos += reclen
    return out


def main(argv):
    args = argv[1:]
    volume = "NEOBENCH"
    rest, i = [], 0
    while i < len(args):
        if args[i] == "--volume" and i + 1 < len(args):
            volume = args[i + 1]
            i += 2
        else:
            rest.append(args[i])
            i += 1
    if len(rest) != 2:
        raise SystemExit("usage: mkiso.py <root-dir> <output.iso> "
                         "[--volume NAME]")
    root, out = rest

    nodes = collect(root)

    # Every identifier is unique -- level 1 can truncate two long names
    # into one, and that must fail here rather than on the reader.
    seen = {}
    for i, node in enumerate(nodes):
        if node[0] is None:
            continue
        ident = iso_name(node[0], node[2])
        key = (node[1], ident)
        if key in seen:
            raise SystemExit("mkiso: {} and {} both map to {}".format(
                seen[key], node[0], ident))
        seen[key] = node[0]

    build(nodes, volume, out)
    listing = verify(out)
    print("mkiso: root: " + ", ".join(listing))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
