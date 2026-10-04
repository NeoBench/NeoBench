#!/usr/bin/env python3
"""
Cut a NeoBench Xen face -- fontxen9.h, fontxen11.h -- out of the Amiga
font of that name.

Xen is NeoBench's standard face: eight pixels of advance, one pixel
strokes, set everywhere the desktop puts type.  It used to be drawn by
hand from the conventions of the period; it is now cut from the Amiga
fonts of the same name, which are held next to this script under
fonts/.  Two sizes of the one face live there -- nine rows, which the
desktop was laid out for, and eleven, which font= in Config/screen.cfg
can ask for -- so nothing here sizes the face: the y size comes out of
the font, and the header's guard, array and height macro take their
names from the file they are cut from, which is what lets the two sit
in one program without either spelling of the face being ambiguous.

An Amiga disk font is a hunk file: one HUNK_CODE carrying a glyph strip
-- every character of the face laid side by side, one bit per pixel and
eight bits to the byte -- and, after the strip, a table holding the x
position and the advance of each character.  This reads that, then
re-cuts the face into NeoBench's own grid:

    the face's own rows      its y size, which is what a row of
                             desktop layout asks for when it sets
                             more than one line
    eight pixels of advance  strw() counts characters and multiplies
                             by eight, and gfx_text steps eight
    glyphs centred in their  so the gap either side of a narrow
    cell                      character is even rather than all
                             falling on one side
    column 7 always clear    so no two glyphs can ever touch

Anything outside 32..126 falls back to the hollow block the console
face uses, so a stray control byte cannot put a hole in the screen.

    usage: build_font_amiga.py fonts/xen9  > fontxen9.h
           build_font_amiga.py fonts/xen11 > fontxen11.h
"""
import struct
import sys

W = 8                        # every face is eight pixels of advance
FIRST, LAST = 32, 126

# The rows the hollow block is drawn in, and the most a face may stand:
# the block has to fit the cell, and nothing on the desktop has a band
# taller than a face of thirty-two rows would want.
BOX_ROWS = 7
MAX_ROWS = 32

HUNK_HEADER = 0x3F3
HUNK_CODE = 0x3E9

# Where the face keeps its numbers, in the code hunk: the y size, then
# the first character it draws (with the last in the byte after it),
# then the start of the glyph strip, the bytes one row of it takes, and
# the offset of the position/advance table that follows the strip.
OFF_YSIZE = 0x4E
OFF_FIRST = 0x5A
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


# Said out loud in the comment at the head of the header: nine and
# eleven read better there than 9 and 11, and a face that grows past
# nineteen is quoted rather than spelled.
WORDS = ["zero", "one", "two", "three", "four", "five", "six", "seven",
         "eight", "nine", "ten", "eleven", "twelve", "thirteen",
         "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
         "nineteen"]


def word(n):
    return WORDS[n] if 0 <= n < len(WORDS) else str(n)


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
    """(rows, first, strip base, row bytes, [(x, advance), ...])."""
    rows = u16(code, OFF_YSIZE)
    first = code[OFF_FIRST]
    last = code[OFF_FIRST + 1]
    strip = u16(code, OFF_STRIP)
    rowbytes = u16(code, OFF_ROWBYTES)
    table = u16(code, OFF_TABLE)

    if rows < BOX_ROWS or rows > MAX_ROWS:
        die("font is %d rows, want %d to %d" % (rows, BOX_ROWS, MAX_ROWS))
    if last < first:
        die("last character %d comes before first character %d" % (last, first))
    if first > FIRST or last < LAST:
        die("font covers characters %d to %d, want %d to %d"
            % (first, last, FIRST, LAST))
    if table < strip or table > len(code) or table % 4:
        die("bad table offset %d" % table)
    if strip + rows * rowbytes > table:
        die("glyph strip would overlap the table")

    pairs = []
    for off in range(table, len(code) - 3, 4):
        pairs.append(struct.unpack(">HH", code[off:off + 4]))
    # One entry per character the face owns, plus one more holding where
    # the last one ends -- which is what lets the check below measure the
    # final character instead of taking it on trust.  A face may begin
    # anywhere (nine starts at 32, eleven at 0), so the entries are
    # indexed from its own first character and not from NeoBench's.
    want = last - first + 2
    if len(pairs) < want:
        die("only %d table entries, want %d" % (len(pairs), want))
    pairs = pairs[:want]

    # The strip is one wide image: each character starts where the last
    # one ended, so the positions must add up.
    for i in range(len(pairs) - 1):
        x, adv = pairs[i]
        if x + adv != pairs[i + 1][0]:
            die("character %d ends at %d, next starts at %d"
                % (first + i, x + adv, pairs[i + 1][0]))
    return rows, first, strip, rowbytes, pairs


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


def rows_for(ch, code, rows, first, strip, rowbytes, pairs):
    """One character as W-wide rows, centred in its cell."""
    if ch < FIRST or ch > LAST:
        art = FALLBACK
        return [(r + "." * W)[:W] for r in art] + ["." * W] * (rows - len(art))

    x, adv = pairs[ch - first]
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
    while len(out) < rows:
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
    path = argv[1]
    try:
        with open(path, "rb") as fh:
            code = code_hunk(fh.read())
    except OSError as exc:
        return "build_font_amiga: %s" % exc

    rows, first, strip, rowbytes, pairs = layout(code)

    # The header is named after the file it comes out of, so two sizes of
    # one face can be in the same program without either name being
    # ambiguous: NBFONTXEN11_H keeps the includes apart, fontxen11 the
    # arrays, NB_XEN11_H the heights.  What every face holds in common is
    # the grid, and that is written once behind an ifndef -- a header
    # that spelled it in its own words would clash with the first face
    # already included.
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    if not name.isidentifier() or not name.isascii():
        return "build_font_amiga: %s: not a name a header can take" % path
    up = name.upper()
    guard = "NBFONT%s_H" % up
    height = "NB_%s_H" % up
    array = "font%s" % name
    what = word(rows)

    print("/* Auto-generated by build_font_amiga.py -- do not edit by hand. */")
    print("#ifndef %s" % guard)
    print("#define %s" % guard)
    print()
    print("/* Xen at %s pixels, cut from the Amiga font of that name:" % what)
    print(" * eight pixel advance, %s rows, each glyph centred in its" % what)
    print(" * cell so column 7 is always clear. */")
    print("#define %-14s%d" % (height, rows))
    print("#ifndef NB_XEN_FIRST")
    print("#define NB_XEN_FIRST  32")
    print("#define NB_XEN_LAST   126")
    print("#define NB_XEN_BOX    95      /* fallback block, after '~'     */")
    print("#endif /* NB_XEN_FIRST: the grid every face is cut to */")
    print()
    print("static const unsigned char %s[96][%d] = {" % (array, rows))
    for c in range(FIRST, LAST + 1):
        art = rows_for(c, code, rows, first, strip, rowbytes, pairs)
        vals = ", ".join("0x%02x" % byte_at(art, r) for r in range(rows))
        print("    { %s },   /* %s */" % (vals, chr(c)))
    art = rows_for(-1, code, rows, first, strip, rowbytes, pairs)  # the box
    vals = ", ".join("0x%02x" % byte_at(art, r) for r in range(rows))
    print("    { %s },   /* box */" % vals)
    print("};")
    print()
    print("#endif /* %s */" % guard)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
