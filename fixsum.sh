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

# Clear existing checksum (first longword)
SUM=$(od -An -td4 -w4 -v "$ROM" | awk '{sum += $1} END {print sum}')
CHECKSUM=$(( (0xFFFFFFFF - SUM) & 0xFFFFFFFF ))

# Calculate checksum

printf "%02X\n" $SUM  # Optional: display SUM for debugging

# Convert and write directly
printf "%08X" $CHECKSUM | xxd -r -p | dd of="$ROM" bs=1 seek=0 count=4 conv=notrunc 2>/dev/null

echo "✅ ROM checksum fixed: $ROM"
