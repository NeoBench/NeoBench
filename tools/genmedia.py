#!/usr/bin/env python3
"""
genmedia.py -- the sample media NeoBench ships, made here rather than
carried.

Six files go into the tree, one for each kind the player reads and one
for each of the three drawers media is kept in:

    Home/Pictures/spectrum.ppm   portable pixmap, P6, three samples a byte
    Home/Pictures/tiles.bmp      Windows bitmap, eight bits and its palette
    Home/Pictures/plasma.png     network graphic, five filters and zlib
    Home/Pictures/rings.iff      IFF ILBM, eight planes and ByteRun1
    Home/Videos/orbit.y4m        YUV4MPEG2, four-two-zero, five frames
    Home/Music/arpeggio.wav      RIFF wave, sixteen bits, two channels

Everything is written from arithmetic that does not depend on the day
it is run, so re-running this changes nothing in the tree unless the
shapes below change with it.  The colours are deliberately smooth: the
desktop quantises every pixel to one hundred and twenty-eight of them
anyway, and a sample that is a field of near-identical colours would be
a sample of the quantiser rather than of itself.

    usage:  python3 tools/genmedia.py
"""

import math
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS = os.path.join(ROOT, "system")


def put(path, data):
    """Write one sample and say what it cost."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    rel = os.path.relpath(path, ROOT)
    print("genmedia: %-34s %7d bytes" % (rel, len(data)))


def clamp(v):
    return 0 if v < 0 else (255 if v > 255 else v)


def hue(t, sat=1.0, val=1.0):
    """A saturated colour around the wheel.  t is 0..1."""
    t = t - math.floor(t)
    i = int(t * 6.0) % 6
    f = t * 6.0 - int(t * 6.0)
    lo = val * (1.0 - sat)                     # where sat leaves it      */
    hi = val
    falling = val * (1.0 - sat * f)            # the wheel coming down    */
    rising = val * (1.0 - sat * (1.0 - f))     # ... and going back up    */
    return ((hi, rising, lo), (falling, hi, lo), (lo, hi, rising),
            (lo, falling, hi), (rising, lo, hi), (hi, lo, falling))[i]


# ------------------------------------------------------------------ *
# Portable pixmap: the plainest picture there is
# ------------------------------------------------------------------ *

def ppm(w, h, fn):
    out = bytearray(b"P6\n%d %d\n255\n" % (w, h))
    for y in range(h):
        for x in range(w):
            out += bytes(fn(x, y))
    return bytes(out)


# ------------------------------------------------------------------ *
# Windows bitmap, eight bits, palette and bottom-up rows
# ------------------------------------------------------------------ *

def bmp8(w, h, fn):
    pal = bytearray()
    for i in range(256):
        if i < 240:
            r, g, b = hue(i / 240.0)
        else:                                   # a grey ramp to the end  */
            v = (i - 240) / 15.0
            r = g = b = v
        pal += bytes((clamp(int(b * 255)), clamp(int(g * 255)),
                      clamp(int(r * 255)), 0))

    body = bytearray()
    for y in range(h - 1, -1, -1):              # bottom up, as it is cut */
        row = bytearray(fn(x, y) & 0xFF for x in range(w))
        row += b"\x00" * ((-len(row)) % 4)      # whole longs a row        */
        body += row

    off = 14 + 40 + 1024
    head = b"BM" + struct.pack("<IHHI", off + len(body), 0, 0, off)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(body),
                       0, 0, 256, 0)
    return head + info + bytes(pal) + bytes(body)


# ------------------------------------------------------------------ *
# Network graphic: zlib, and one of each of the five row filters
# ------------------------------------------------------------------ *

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def png(w, h, fn):
    raw = bytearray()
    prev = bytes(w * 3)
    for y in range(h):
        line = bytes(b for x in range(w) for b in fn(x, y))
        f = y % 5                               # 0 1 2 3 4, round       */
        out = bytearray(w * 3)
        for i in range(w * 3):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 0:
                v = line[i]
            elif f == 1:
                v = line[i] - a
            elif f == 2:
                v = line[i] - b
            elif f == 3:
                v = line[i] - ((a + b) // 2)
            else:
                v = line[i] - _paeth(a, b, c)
            out[i] = v & 0xFF
        raw.append(f)
        raw += out
        prev = line

    comp = zlib.compress(bytes(raw), 9)
    half = len(comp) // 2                       # two runs of image data  */

    def chunk(typ, data):
        d = typ + data
        return (struct.pack(">I", len(data)) + d +
                struct.pack(">I", zlib.crc32(d) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", comp[:half]) +
            chunk(b"IDAT", comp[half:]) +
            chunk(b"IEND", b""))


# ------------------------------------------------------------------ *
# IFF ILBM: eight planes, ByteRun1 over the whole body at once
# ------------------------------------------------------------------ *

def byterun1(data):
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:                            # worth saying twice over  */
            out.append((1 - run) & 0xFF)
            out.append(data[i])
            i += run
            continue

        j = i
        while j < n and (j - i) < 128:
            if j + 2 < n and data[j] == data[j + 1] == data[j + 2]:
                break                           # a run starts: stop here  */
            j += 1
        if j == i:                              # one byte before a run    */
            j = i + 1
        out.append(j - i - 1)
        out += data[i:j]
        i = j
    return bytes(out)


def iff(w, h, fn):
    planes, rowb = 8, (w + 7) // 8
    body = bytearray()
    for y in range(h):
        idx = [fn(x, y) & 0xFF for x in range(w)]
        for p in range(planes):                 # row across row          */
            for bx in range(rowb):
                v = 0
                for bit in range(8):
                    x = bx * 8 + bit
                    if x < w and (idx[x] >> p) & 1:
                        v |= 0x80 >> bit
                body.append(v)

    cmap = bytearray()
    for i in range(256):
        r, g, b = hue(i / 256.0)
        cmap += bytes((clamp(int(r * 255)), clamp(int(g * 255)),
                       clamp(int(b * 255))))

    bmhd = struct.pack(">HHhhBBBBHBBHH", w, h, 0, 0, planes, 0, 1, 0,
                       0, 1, 1, w, h)

    def chunk(typ, data):
        out = struct.pack(">4sI", typ, len(data)) + data
        if len(data) & 1:
            out += b"\x00"                      # the pad the spec keeps  */
        return out

    inner = (b"ILBM" +
             chunk(b"BMHD", bmhd) +
             chunk(b"CMAP", bytes(cmap)) +
             chunk(b"BODY", byterun1(bytes(body))))
    return b"FORM" + struct.pack(">I", len(inner)) + inner


# ------------------------------------------------------------------ *
# YUV4MPEG2, four-two-zero: the film the player steps through
# ------------------------------------------------------------------ *

def y4m(w, h, frames, fps, fn):
    out = bytearray(("YUV4MPEG2 W%d H%d F%d:1 Ip A1:1 C420\n"
                     % (w, h, fps)).encode("ascii"))
    for k in range(frames):
        out += b"FRAME\n"
        ys = bytearray(w * h)
        us = bytearray(w * h // 4)
        vs = bytearray(w * h // 4)
        for y in range(h):
            for x in range(w):
                r, g, b = fn(x, y, k)
                # the forward half of the pair the player inverts:
                # studio swing, sixteen to two-three-five
                yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16
                uu = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128
                vv = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128
                ys[y * w + x] = clamp(yy)
                if x % 2 == 0 and y % 2 == 0:
                    i = (y // 2) * (w // 2) + (x // 2)
                    us[i] = clamp(uu)
                    vs[i] = clamp(vv)
        out += ys + us + vs
    return bytes(out)


# ------------------------------------------------------------------ *
# RIFF wave: sixteen bits, two channels, so the downmix is exercised
# ------------------------------------------------------------------ *

def wav(rate, frames, channels, bits, fn):
    body = bytearray()
    for i in range(frames):
        for ch in range(channels):
            v = fn(i, ch)
            if bits == 8:
                body.append(clamp(v + 128))
            else:
                body += struct.pack("<h", max(-32768, min(32767, v)))
    block = channels * bits // 8
    head = (b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVE" +
            b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate,
                                  rate * block, block, bits) +
            b"data" + struct.pack("<I", len(body)))
    return head + bytes(body)


# ------------------------------------------------------------------ *
# The six shapes
# ------------------------------------------------------------------ *

def spectrum(x, y):                             # a band of colour        */
    t = x / 47.0
    r, g, b = hue(t)
    if y > 24:                                  # a shadow under the band */
        r, g, b = r * 0.3, g * 0.3, b * 0.3
    return (int(r * 255), int(g * 255), int(b * 255))


def tiles(x, y):                                # a floor of two tiles    */
    if x < 2 or y < 2 or x > 45 or y > 29:
        return 252                              # the grey ramp's white   */
    base = 160 if ((x // 6) + (y // 6)) & 1 else 20
    return (base + (x % 6) * 3 + (y % 6)) & 0xFF


def plasma(x, y):                               # three sines over a field */
    v = (math.sin(x / 13.0) + math.sin(y / 11.0) +
         math.sin((x + y) / 17.0)) / 3.0
    r = 127 * (1 + math.sin(v * 3.1))
    g = 127 * (1 + math.sin(v * 3.1 + 2.094))
    b = 127 * (1 + math.sin(v * 3.1 + 4.188))
    return (int(r), int(g), int(b))


def rings(x, y):                                # round the centre        */
    dx, dy = x - 32, y - 24
    d = int(math.sqrt(dx * dx + dy * dy))
    return (d * 6 + 8) & 0xFF


def orbit(x, y, k, w, h, frames):               # a lamp going round      */
    r = g = b = 40 if (x % 9 == 0 or y % 9 == 0) else 14
    ang = k / float(frames) * math.pi * 2.0
    cx = w * 0.5 + (w * 0.5 - 7.0) * math.cos(ang)
    cy = h * 0.5 + (h * 0.5 - 5.0) * math.sin(ang)
    if (x - cx) ** 2 + (y - cy) ** 2 <= 25.0:
        return (255, 210, 60)
    if (x - w // 2) ** 2 + (y - h // 2) ** 2 <= 9:
        return (60, 200, 255)
    return (r, g, b)


def arpeggio(i, ch):                            # four notes, one channel */
    rate, frames = 8000, 2000
    t = i / float(rate)
    notes = (440.0, 554.37, 659.25, 880.0)
    f = notes[min(3, int(t / 0.0625))]
    env = min(1.0, i / 160.0, (frames - i) / 320.0)
    amp = 22000 if ch == 0 else 11000          # the right, a shade down  */
    return int(amp * math.sin(2 * math.pi * f * t) * env)


def main():
    put(os.path.join(SYS, "Home/Pictures/spectrum.ppm"), ppm(48, 32, spectrum))
    put(os.path.join(SYS, "Home/Pictures/tiles.bmp"), bmp8(48, 32, tiles))
    put(os.path.join(SYS, "Home/Pictures/plasma.png"), png(96, 64, plasma))
    put(os.path.join(SYS, "Home/Pictures/rings.iff"), iff(64, 48, rings))
    put(os.path.join(SYS, "Home/Videos/orbit.y4m"),
        y4m(72, 36, 5, 5, lambda x, y, k: orbit(x, y, k, 72, 36, 5)))
    put(os.path.join(SYS, "Home/Music/arpeggio.wav"),
        wav(8000, 2000, 2, 16, arpeggio))
    return 0


if __name__ == "__main__":
    sys.exit(main())
