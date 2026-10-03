#!/usr/bin/env python3
"""
Cut fontxen9.h -- NeoBench's Xen face -- out of the Amiga Xen font.

Xen is NeoBench's standard face: eight pixels of advance, nine rows, one
pixel strokes, set everywhere the desktop puts type.  It used to be drawn
by hand from the conventions of the period; it is now cut from the Amiga
font of the same name, which is held next to this script under fonts/.

An Amiga disk font is a hunk file: one HUNK_CODE carrying a glyph strip
-- every character of the face laid side by side, one bit per pixel and
eight bits to the byte -- and, after the strip, a table holding the x
position and the advance of each character.  This reads that, then
re-cuts the face into NeoBench's own grid:

    nine rows                the face's own y size, which is what
                             every row of desktop layout assumes
    eight pixels of advance  strw() counts characters and multiplies
                             by eight, and gfx_text steps eight
    glyphs centred in their  so the gap either side of a narrow
    cell                      character is even rather than all
                             falling on one side
    column 7 always clear    so no two glyphs can ever touch

Anything outside 32..126 falls back to the hollow block the console
face uses, so a stray control byte cannot put a hole in the screen.

    usage: build_font_amiga.py fonts/xen9 > fontxen9.h
"""
import struct
import sys

W, H = 8, 9
FIRST, LAST = 32, 126

HUNK_HEADER = 0x3F3
HUNK_CODE = 0x3E9

# Where the face keeps its numbers, in the code hunk: the y size, then
# the start of the glyph strip, the bytes one row of it takes, and the
# offset of the position/advance table that follows the strip.
OFF_YSIZE = 0x4E
OFF_STRIP = 0x5E
OFF_ROWBYTES = 0x60
OFF_TABLE = 0x64

# What an out-of-range character becomes: a hollow block, as the console
# face does, so nothing can print as an invisible hole.
FALLBACK = [
    ".#####.",
    "#.....#",
    "#.....#",
    "#.....#",
    "#.....#",
    "#.....#",
    ".#####.",
]


def die(msg):
    raise SystemExit("build_font_amiga: " + msg)


def u16(buf, off):
    if off + 2 > len(buf):
        die("font is truncated at offset %d" % off)
    return struct.unpack(">H", buf[off:off + 2])[0]


def code_hunk(data):
    """The single HUNK_CODE of the file: the whole font."""
    if len(data) < 4:
        die("not an Amiga hunk file")
    words = struct.unpack(">%dI" % (len(data) // 4), data[:len(data) // 4 * 4])
    if not words or words[0] != HUNK_HEADER:
        die("missing HUNK_HEADER (0x3f3)")

    i = 1
    nlen = words[i]                     # resident library name, padded
    i += 1 + (nlen + 3) // 4
    if i + 3 >= len(words):
        die("truncated hunk header")
    table = words[i]                    # hunks in the file
    first, last = words[i + 1], words[i + 2]
    i += 3 + table                      # and their sizes in longs

    if i + 1 >= len(words):
        die("truncated hunk header")
    if first != 0 or last != 0 or table != 1:
        die("expected one hunk, got table=%d %d..%d" % (table, first, last))
    tag, longs = words[i], words[i + 1]
    i += 2
    if tag != HUNK_CODE:
        die("expected HUNK_CODE (0x3e9), got 0x%03x" % tag)
    start = i * 4
    if start + longs * 4 > len(data):
        die("code hunk runs past the end of the file")
    return data[start:start + longs * 4]


def layout(code):
    """(rows, strip base, row bytes, [(x, advance), ...])."""
    rows = u16(code, OFF_YSIZE)
    strip = u16(code, OFF_STRIP)
    rowbytes = u16(code, OFF_ROWBYTES)
    table = u16(code, OFF_TABLE)

    if rows != H:
        die("font is %d rows, want %d" % (rows, H))
    if table < strip or table > len(code) or table % 4:
        die("bad table offset %d" % table)
    if strip + rows * rowbytes > table:
        die("glyph strip would overlap the table")

    pairs = []
    for off in range(table, len(code) - 3, 4):
        pairs.append(struct.unpack(">HH", code[off:off + 4]))
    if len(pairs) < LAST - FIRST + 1:
        die("only %d characters, want %d" % (len(pairs), LAST - FIRST + 1))
    pairs = pairs[:LAST - FIRST + 1]

    # The strip is one wide image: each character starts where the last
    # one ended, so the positions must add up.
    for i in range(len(pairs) - 1):
        x, adv = pairs[i]
        if x + adv != pairs[i + 1][0]:
            die("character %d ends at %d, next starts at %d"
                % (FIRST + i, x + adv, pairs[i + 1][0]))
    return rows, strip, rowbytes, pairs


def glyph(code, rows, strip, rowbytes, x, adv):
    """The character's bits, one integer per row, `adv` bits wide."""
    out = []
    for r in range(rows):
        line = code[strip + r * rowbytes: strip + r * rowbytes + rowbytes]
        bits = 0
        for b in range(adv):
            bit = x + b
            if bit // 8 >= len(line):
                die("character runs past the end of its row")
            bits = (bits << 1) | ((line[bit // 8] >> (7 - bit % 8)) & 1)
        out.append(bits)
    return out


def rows_for(ch, code, rows, strip, rowbytes, pairs):
    """One character as W-wide rows, centred in its cell."""
    if ch < FIRST or ch > LAST:
        art = FALLBACK
        return [(r + "." * W)[:W] for r in art] + ["." * W] * (H - len(art))

    x, adv = pairs[ch - FIRST]
    if adv > W:
        die("character %r advances %d, wider than the %d pixel cell"
            % (chr(ch), adv, W))
    # Centred, but never far enough right to touch column 7.
    inset = (W - adv) // 2
    if inset + adv > W:
        die("character %r does not fit its cell" % chr(ch))

    bits = glyph(code, rows, strip, rowbytes, x, adv)
    out = []
    for v in bits:
        byte = v << (W - inset - adv)
        out.append("".join("#" if byte & (0x80 >> c) else "."
                           for c in range(W)))
    while len(out) < H:
        out.append("." * W)
    return out


def byte_at(art, row):
    b = 0
    for i, c in enumerate(art[row]):
        if c == '#':
            b |= 1 << (7 - i)
    return b


def main(argv):
    if len(argv) != 2:
        return 'usage: build_font_amiga.py fonts/xen9 > fontxen9.h'
    try:
        with open(argv[1], "rb") as fh:
            code = code_hunk(fh.read())
    except OSError as exc:
        return "build_font_amiga: %s" % exc

    rows, strip, rowbytes, pairs = layout(code)

    print("/* Auto-generated by build_font_amiga.py -- do not edit by hand. */")
    print("#ifndef NBFONTXEN9_H")
    print("#define NBFONTXEN9_H")
    print()
    print("/* Xen at nine pixels, cut from the Amiga font of that name:")
    print(" * eight pixel advance, nine rows, each glyph centred in its")
    print(" * cell so column 7 is always clear. */")
    print("#define NB_XEN_H      9")
    print("#define NB_XEN_FIRST  32")
    print("#define NB_XEN_LAST   126")
    print("#define NB_XEN_BOX    95      /* fallback block, after '~'     */")
    print()
    print("static const unsigned char fontxen9[96][9] = {")
    for c in range(FIRST, LAST + 1):
        art = rows_for(c, code, rows, strip, rowbytes, pairs)
        vals = ", ".join("0x%02x" % byte_at(art, r) for r in range(H))
        print("    { %s },   /* %s */" % (vals, chr(c)))
    art = rows_for(-1, code, rows, strip, rowbytes, pairs)   # the box
    vals = ", ".join("0x%02x" % byte_at(art, r) for r in range(H))
    print("    { %s },   /* box */" % vals)
    print("};")
    print()
    print("#endif /* NBFONTXEN9_H */")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
