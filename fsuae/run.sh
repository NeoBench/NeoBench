#!/bin/bash
# NeoBench FS-UAE launcher
# Builds HDF if needed and starts FS-UAE

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
HDF_IMAGE="$PROJECT_DIR/images/neobench.hdf"
KERNEL="$PROJECT_DIR/rootfs/boot/kernel.elf"
CONFIG="$SCRIPT_DIR/NeoBench-060.fs-uae"

echo "NeoBench FS-UAE Launcher"
echo "========================"

# Build HDF if needed
if [ ! -f "$HDF_IMAGE" ] || [ "$KERNEL" -nt "$HDF_IMAGE" ]; then
    echo "Building HDF image..."
    python3 "$PROJECT_DIR/scripts/build_hdf.py" "$KERNEL" "$HDF_IMAGE" 16
    echo ""
fi

# Check for FS-UAE
if ! command -v fs-uae &> /dev/null; then
    echo "Error: FS-UAE not found in PATH"
    echo "Install: sudo apt install fs-uae fs-uae-arcade"
    exit 1
fi

echo "Starting FS-UAE..."
echo "Config: $CONFIG"
echo ""

fs-uae "$CONFIG"
