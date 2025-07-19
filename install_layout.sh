#!/bin/bash
# neobench_layout.sh — Prepare NeoBench OS layout (run from /home/adolf/neobench)
ROOT="${1:-.}"

dirs=(
  C L Libs Libs/datatype Devs Fonts Prefs S Programs Internet Temp
  NeoBench NeoBench/Backdrops NeoBench/Icons
)

files=(
  L/PFS3_AIO
  Devs/mountlist
  Programs/HDToolBox
  Prefs/NeoBenchPrefs
  Devs/crossdos.device
  Devs/usbdisk.device
  Libs/keymap.library
  Libs/icon.library
  NeoBench/Icons/neobench.info
  NeoBench/Backdrops/backdrop1.iff
  NeoBench/Backdrops/backdrop2.iff
  NeoBench/Backdrops/splash.iff
  Programs/NeoBenchInfo
  Programs/NeoBenchCalc
  Programs/NeoBenchCalendar
  Programs/NeoBenchHDToolBox
  C/NeoBenchShell
  Internet/NeoBenchUpdate
  Devs/NeoBenchBluetooth
  Devs/NeoBenchUSB
  Libs/datatype/png.datatype
  Libs/datatype/modernsound.datatype
)

echo "== Making NeoBench directory structure =="
for d in "${dirs[@]}"; do
  mkdir -p "$ROOT/$d"
done

echo "== Creating placeholder binaries/files =="
for f in "${files[@]}"; do
  touch "$ROOT/$f"
done

cat > "$ROOT/S/startup-sequence" <<EOF
NeoBench booting...
SetPatch QUIET
Assign NeoBench: $ROOT/NeoBench
cd NeoBench:
run >nil: C/NeoBenchShell
run >nil: NeoBench/NeoBenchBar
EOF

cat > "$ROOT/S/user-startup" <<EOF
echo NeoBench Boot Complete.
EOF

echo "== Layout complete. You can now add real binaries, datatypes, backdrops, etc. =="
