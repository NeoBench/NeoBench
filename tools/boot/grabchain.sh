#!/bin/bash
# grabchain.sh [kicks] -- chainload boot: genuine AmigaOS 3.2.3 comes up
# from the NeoBench32 volume, S/Startup-sequence runs SYS:NeoBench (the
# hunk built by "make -C boot/rom chain"), and NBCHAIN hands the machine
# to it.  Summarises NBCHAIN, the boot log tallies and the two Rust
# answers, the way grabtest.sh does for the bare-ROM boot.
#
# The volume is a complete AmigaOS 3.2.3 install whose Startup-sequence
# carries the chain block ("IF EXISTS SYS:NeoBench / SYS:NeoBench"); the
# hunk at its root is copied in from the build before every run, since
# S: execs exactly that file.  NB_CHAIN_VOLUME points somewhere else if
# the volume lives elsewhere.  The model and memory are grabtest's --
# A4000 with 136 MB -- and the kickstart defaults to the genuine
# AmigaOS 3.2.3 A4000 ROM beside the A1200 one.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
KICKS="${1:-/home/lordp/Documents/FS-UAE/Kickstarts/A4000.47.115.rom}"
VOL="${NB_CHAIN_VOLUME:-/home/lordp/Documents/FS-UAE/Hard Drives/NeoBench32}"
HUNK="$ROOT/boot/rom/NeoBench"
PORT=19424
CFG="$HERE/nbchain.fs-uae"
SERIAL="$HERE/chain.log"
MAXWAIT=240

if [ ! -f "$KICKS" ]; then
    echo "grabchain: no such kickstart: $KICKS" >&2
    exit 1
fi
if [ ! -f "$HUNK" ]; then
    echo "grabchain: no such hunk: $HUNK" >&2
    exit 1
fi

# The hunk on the volume is what S:Startup-sequence execs; keep it the
# one the build just produced.
cp -f "$HUNK" "$VOL/NeoBench" || exit 1
chmod +x "$VOL/NeoBench"

pkill -x fs-uae 2>/dev/null || true
sleep 8
rm -f "$SERIAL"

cat > "$CFG" <<EOF
[fs-uae]
amiga_model = A4000
cpu = 68060
chip_memory = 2048
motherboard_ram = 8192
zorro_iii_memory = 131072
kickstart_file = $KICKS
hard_drive_0 = $VOL
floppy_drive_0 = 0
fullscreen = 0
assume_refresh_rate = 60
fast_memory = 8
uae_fastmem_autoconfig = false
serial_port = tcp://127.0.0.1:$PORT/wait
volume = 0
window_title = NeoBench chaingrab
EOF

XAUTH="$(ls /run/user/1000/xauth_* 2>/dev/null | head -1)"
[ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
export DISPLAY=:0
export SDL_VIDEODRIVER=x11
unset WAYLAND_DISPLAY
unset LD_LIBRARY_PATH

attempt=0
while [ "$attempt" -lt 4 ]; do
    attempt=$((attempt + 1))
    echo "grabchain: launch attempt $attempt (kicks=$KICKS)"

    fs-uae "$CFG" >"$HERE/fs-uae.out" 2>&1 &
    EMUPID=$!

    sleep 2
    if ! kill -0 "$EMUPID" 2>/dev/null; then
        echo "grabchain: fs-uae died early:" >&2
        tail -5 "$HERE/fs-uae.out" >&2
        sleep 4
        continue
    fi

    python3 "$HERE/serialcap.py" "$PORT" "$SERIAL" $((MAXWAIT + 60)) &
    CAPPID=$!

    # AmigaOS boots first and serial lag past a minute is normal, so
    # this waits long after the last line lands.
    waited=0
    lastsize=-1
    while [ "$waited" -lt "$MAXWAIT" ]; do
        sleep 5
        waited=$((waited + 5))
        [ -f "$SERIAL" ] || continue
        size=$(stat -c%s "$SERIAL")
        if grep -q '>rs prefs ok' "$SERIAL" 2>/dev/null &&
           [ "$size" -eq "$lastsize" ] && [ "$waited" -ge 90 ]; then
            break
        fi
        lastsize=$size
    done

    sleep 10
    pkill -x fs-uae 2>/dev/null || true
    kill "$CAPPID" 2>/dev/null || true
    sleep 8

    echo "=== grabchain summary ==="
    if [ ! -f "$SERIAL" ]; then
        echo "grabchain: no serial output" >&2
        tail -10 "$HERE/fs-uae.out" >&2
        continue
    fi
    echo "bytes: $(stat -c%s "$SERIAL")"
    echo "WARNs : $(grep -c ' WARN ' "$SERIAL" || true)"
    echo "FAILED: $(grep -c 'FAILED' "$SERIAL" || true)"
    grep -E 'NBCHAIN' "$SERIAL" | head -4 || true
    grep -E '>rs( prefs)? (ok|fail)' "$SERIAL" || true
    grep -E '>present|>ui' "$SERIAL" | tail -4 || true
    exit 0
done

echo "grabchain: all launch attempts failed" >&2
exit 1
