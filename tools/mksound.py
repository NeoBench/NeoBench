#!/usr/bin/env python3
"""Build the NeoBench startup sound.

    python3 tools/mksound.py --wav system/Core/Media/neoboot.wav \
                             system/Core/Media/startup.snd

With --wav the sound is taken from a WAV file -- the recording the user
has chosen to greet the boot with -- and converted; without it the tool
synthesises NeoBench's own four note chime.  Either way the sample is
written as NSND -- a 16 byte big endian header followed by signed 8 bit
mono data -- because Paula wants raw signed bytes and the kernel would
otherwise have nowhere to learn the sample rate:

      0  "NSND"
      4  period     Paula period, in colour clock ticks
      6  channels   1
      8  length     sample bytes, even
     12  rate       samples per second as generated

The period is chosen here, on a host that has division, as the closest
clock tick count to the source file's own rate, so the conversion does
nothing more than resample to it: 8 000 Hz becomes period 443, which is
3 546 895 / 443 = 8 006 samples a second.  The division has to happen
off the machine anyway -- the freestanding link carries no libgcc, so a
divide by a runtime value in the kernel is a link error.

What comes out with no --wav is a rising four note arpeggio over a
sustained fifth below it -- a boot chime rather than a jingle, short
enough that the machine is waiting for input by the time it decays.
"""

import argparse
import math
import struct
import sys
import wave

# Paula's period register counts colour clock ticks: 3 546 895 Hz on PAL.
# The NDK's audio autodoc quotes 279.365 ns, which is the NTSC figure
# (3 579 545 Hz); the machine under test is a PAL A1200.
PAL_CLOCK = 3546895

# 124 is the lowest period the audio device documents, and it is low
# enough that the chime is not short of bandwidth: 3 546 895 / 124 =
# 28 604 samples a second, with nothing above a couple of kilohertz in
# the waveform to alias.
PERIOD_MIN = 124

ATTACK = 0.008           # note attack, seconds
CHORUS_DETUNE = 1.006     # second oscillator, for a little width
LEAD = 0.15               # silence put in front of the sound, seconds


def envelope(t, tau):
    """Attack and exponential decay for one note, t seconds since onset."""
    if t < 0.0:
        return 0.0
    a = t / ATTACK if t < ATTACK else 1.0
    return a * math.exp(-t / tau)


def note(t, freq, amp, tau):
    """One partial stack: fundamental, two harmonics, and the detune."""
    e = envelope(t, tau)
    if e < 1e-4:
        return 0.0
    w = (math.sin(2.0 * math.pi * freq * t) +
         0.30 * math.sin(4.0 * math.pi * freq * t) +
         0.12 * math.sin(6.0 * math.pi * freq * t) +
         0.25 * math.sin(2.0 * math.pi * freq * CHORUS_DETUNE * t))
    return amp * e * w


def synthesise(rate, seconds):
    """The chime as floats in -1..1, one per sample."""
    # (onset, Hz, amplitude, decay time constant)
    voice = [
        (0.00, 220.00, 0.34, 0.85),   # A3, the floor under everything
        (0.00, 440.00, 0.90, 0.32),   # A4
        (0.15, 554.37, 0.85, 0.32),   # C#5
        (0.30, 659.25, 0.85, 0.34),   # E5
        (0.45, 880.00, 0.95, 0.50),   # A5, the landing note
        (0.62, 987.77, 0.30, 0.30),   # B5 shimmer over the landing
    ]

    n = int(rate * seconds)
    out = [0.0] * n
    for onset, freq, amp, tau in voice:
        start = int(onset * rate)
        for i in range(start, n):
            out[i] += note((i - start) / rate, freq, amp, tau)

    peak = max(max(out), -min(out)) or 1.0
    scale = 0.82 / peak

    # The last note is still sounding when the buffer runs out; fade the
    # tail so Paula does not stop mid-waveform on a discontinuity.
    fade = int(0.025 * rate)
    for i in range(n):
        s = out[i] * scale
        if i >= n - fade:
            s *= (n - i) / fade
        out[i] = s
    return out


def pack(samples, period, rate):
    data = bytearray()
    for s in samples:
        v = int(round(s * 127.0))
        if v > 127:
            v = 127
        elif v < -128:
            v = -128
        data.append(v & 0xFF)
    if len(data) & 1:            # Paula length is counted in whole words
        del data[-1:]
    header = struct.pack(">4sHHII", b"NSND", period, 1, len(data), rate)
    return header + bytes(data)


def read_wav(path):
    """The file's samples as floats in -1..1, and its rate in Hz."""
    with wave.open(path, "rb") as w:
        width = w.getsampwidth()
        chans = w.getnchannels()
        src_rate = w.getframerate()
        raw = w.readframes(w.getnframes())

    if width == 1:                      # WAV's 8 bit PCM is unsigned
        vals = [(b - 128) / 128.0 for b in raw]
    elif width == 2:                    # signed little endian
        vals = []
        for i in range(0, len(raw) - 1, 2):
            v = raw[i] | (raw[i + 1] << 8)
            if v >= 32768:
                v -= 65536
            vals.append(v / 32768.0)
    else:
        raise SystemExit("mksound: %d byte samples are not supported"
                         % width)

    if chans > 1:                       # one channel is all Paula has
        vals = [sum(vals[i:i + chans]) / chans
                for i in range(0, len(vals), chans)]

    if not vals:
        raise SystemExit("mksound: %s has no samples" % path)
    return vals, src_rate


def resample(vals, src_rate, dst_rate):
    """Linear interpolation: the source rate is close to Paula's, so the
    error at each step is a fraction of the noise already in the file."""
    if src_rate == dst_rate:
        return vals

    n = max(2, int(round(len(vals) * dst_rate / src_rate)))
    step = src_rate / dst_rate
    last = len(vals) - 1
    out = []
    for i in range(n):
        p = i * step
        j = int(p)
        if j >= last:
            out.append(vals[last])
        else:
            f = p - j
            out.append(vals[j] * (1.0 - f) + vals[j + 1] * f)
    return out


def fades(vals, rate):
    """Two milliseconds in, five out: Paula stops dead when the length
    register runs out, and a wave that stops mid-swing is a click."""
    na = max(1, int(0.002 * rate))
    nr = max(1, int(0.005 * rate))
    for i in range(min(na, len(vals))):
        vals[i] *= i / na
    for k in range(min(nr, len(vals))):
        vals[-1 - k] *= k / nr
    return vals


def lead(vals, rate):
    """Silence in front of the sound.

    A Paula buffer is not stopped by running out: Agnus reloads the
    address and the length and plays it again, so a sound that is asked
    to sound once is a sound somebody has to stop after a measured time.
    That stop cannot be hung on the channel's own completion request --
    on the machine under test the request is raised by enabling a
    channel whose length has already been loaded and never raised again,
    so polling it either passes before the sound has started or never
    passes at all -- and a measured stop is quantised to the field, one
    fiftieth of a second, which is far coarser than the margin between
    the sound ending and the repeat starting.  Fifteen hundredths of a
    second of silence gives that stop somewhere to land: after the sound
    has finished and before the repeat gets back to the attack, which is
    seven and a half fields of room either side.
    """
    return [0.0] * int(round(LEAD * rate)) + list(vals)


def main(argv):
    ap = argparse.ArgumentParser(description="build the NeoBench startup sound")
    ap.add_argument("out", nargs="?", default="system/Core/Media/startup.snd")
    ap.add_argument("--wav", help="take the sound from this WAV file instead "
                                  "of synthesising the chime (--period is "
                                  "then derived from the file's own rate)")
    ap.add_argument("--clock", type=int, default=PAL_CLOCK,
                    help="Paula period clock in Hz (default: PAL)")
    ap.add_argument("--period", type=int, default=PERIOD_MIN,
                    help="sample period in clock ticks (default: 124)")
    ap.add_argument("--seconds", type=float, default=1.15)
    args = ap.parse_args(argv[1:])

    if args.wav:
        vals, src_rate = read_wav(args.wav)

        period = (args.clock + src_rate // 2) // src_rate   # nearest tick
        if period < PERIOD_MIN:       # Paula will not go faster than this
            period = PERIOD_MIN
        elif period > 65535:
            period = 65535

        rate = args.clock // period
        vals = lead(fades(resample(vals, src_rate, rate), rate), rate)
        blob = pack(vals, period, rate)
        seconds = len(vals) / rate
        label = args.wav
    else:
        if args.period < 1:
            sys.stderr.write("mksound: period must be positive\n")
            return 2
        period = args.period
        rate = args.clock // period
        blob = pack(lead(synthesise(rate, args.seconds), rate), period, rate)
        seconds = args.seconds + LEAD
        label = "the chime"

    with open(args.out, "wb") as fh:
        fh.write(blob)

    print("mksound: %s, %.2f s, period %u, %u Hz, %u bytes -> %s"
          % (label, seconds, period, rate, len(blob) - 16, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
