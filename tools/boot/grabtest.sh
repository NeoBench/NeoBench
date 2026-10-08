#!/bin/bash
# grabtest.sh <rom> -- boot a NeoBench ROM under FS-UAE and capture the
# serial line, then summarise the markers the gate reads: WARN / FAILED
# counts from the boot log, and the >rs / >rs prefs answers from the two
# Rust checks.  Bare-ROM boot: the ROM is the kickstart.
#
# The config is the one the 11-WARN / 0-FAILED baselines were taken
# under, recovered after a /tmp wipe lost the original: A4000, 68060,
# chip 2048, motherboard_ram 8192 + zorro_iii_memory 131072 = the 136 MB
# nb_probe_fast_mb() wants (the line reads 144 MB and goes green).  An
# A1200 could not be brought to that number in FS-UAE: non-autoconfig
# fast_memory stops at 8 MB, motherboard_ram at 256 MB came back
# "Unsupported Mainboard RAM size", and accelerator_memory was ignored
# -- so the model is not a free choice.  The floor itself is
# NB_FAST_FLOOR in boot/rom/probe.h: below 128 MB the boot says
# [FAILED] Memory detected, and that failure is harness configuration,
# never the code under test.
#
# Launch semantics match the original (pkill -x fs-uae only, sleep 8, up
# to 4 launch retries, serial lag tolerated by polling well past the
# last expected line).
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ROM="${1:?usage: grabtest.sh <rom>}"
PORT=19423
CFG="$HERE/nbtest.fs-uae"
SERIAL="$HERE/serial.log"
MAXWAIT=180

if [ ! -f "$ROM" ]; then
    echo "grabtest: no such ROM: $ROM" >&2
    exit 1
fi

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
kickstart_file = $ROM
floppy_drive_0 = 0
fullscreen = 0
assume_refresh_rate = 60
fast_memory = 8
uae_fastmem_autoconfig = false
serial_port = tcp://127.0.0.1:$PORT/wait
volume = 0
window_title = NeoBench grabtest
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
    echo "grabtest: launch attempt $attempt (rom=$ROM)"

    fs-uae "$CFG" >"$HERE/fs-uae.out" 2>&1 &
    EMUPID=$!

    sleep 2
    if ! kill -0 "$EMUPID" 2>/dev/null; then
        echo "grabtest: fs-uae died early:" >&2
        tail -5 "$HERE/fs-uae.out" >&2
        sleep 4
        continue
    fi

    python3 "$HERE/serialcap.py" "$PORT" "$SERIAL" $((MAXWAIT + 60)) &
    CAPPID=$!

    # Poll for the two answers and a settled log.  Serial lag past a
    # minute is normal, so this waits long after the last line lands.
    waited=0
    lastsize=-1
    while [ "$waited" -lt "$MAXWAIT" ]; do
        sleep 5
        waited=$((waited + 5))
        [ -f "$SERIAL" ] || continue
        size=$(stat -c%s "$SERIAL")
        if grep -q '>rs prefs ok' "$SERIAL" 2>/dev/null &&
           [ "$size" -eq "$lastsize" ] && [ "$waited" -ge 60 ]; then
            break
        fi
        lastsize=$size
    done

    # Give the tail of the log time to arrive, then tear down.
    sleep 10
    pkill -x fs-uae 2>/dev/null || true
    kill "$CAPPID" 2>/dev/null || true
    sleep 8

    echo "=== grabtest summary ==="
    if [ ! -f "$SERIAL" ]; then
        echo "grabtest: no serial output" >&2
        tail -10 "$HERE/fs-uae.out" >&2
        continue
    fi
    echo "bytes: $(stat -c%s "$SERIAL")"
    echo "WARNs : $(grep -c ' WARN ' "$SERIAL" || true)"
    echo "FAILED: $(grep -c 'FAILED' "$SERIAL" || true)"
    grep -E '>rs( prefs)? (ok|fail)' "$SERIAL" || true
    grep -E '>prefs|>present|>ui' "$SERIAL" | tail -6 || true
    exit 0
done

echo "grabtest: all launch attempts failed" >&2
exit 1
