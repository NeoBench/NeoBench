#!/bin/bash

ROM="$1"

if [[ ! -f "$ROM" ]]; then
  echo "Error: ROM file not found."
  exit 1
fi

if [[ $(stat -c%s "$ROM") -ne 524288 ]]; then
  echo "Error: ROM must be exactly 512KB (524288 bytes)"
  exit 1
fi

# Zero the first 4 bytes (checksum area)
printf '\x00\x00\x00\x00' | dd of="$ROM" bs=1 seek=0 count=4 conv=notrunc &>/dev/null

# Compute checksum using Perl (handles unsigned 32-bit correctly)
SUM=$(od -An -N524288 -t u4 -v "$ROM" | \
  perl -ne '
    for (split) { $s += $_ }
    END { printf "%08X\n", (0xFFFFFFFF - $s) & 0xFFFFFFFF }
  ')

# Split hex into bytes
B1=${SUM:0:2}
B2=${SUM:2:2}
B3=${SUM:4:2}
B4=${SUM:6:2}

# Write checksum into first 4 bytes
printf "\\x$B1\\x$B2\\x$B3\\x$B4" | dd of="$ROM" bs=1 seek=0 count=4 conv=notrunc &>/dev/null

echo "✅ ROM checksum fixed: $ROM"
