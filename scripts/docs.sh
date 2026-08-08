#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

DOCROOT="docs/specifications/NBFS"

echo "========================================"
echo " Creating NBFS Documentation"
echo "========================================"

mkdir -p "$DOCROOT"

cat > "$DOCROOT/README.md" <<EOF
# NeoBench File System

NBFS Documentation

Status: Draft

Current Version: 1.0

This directory contains the complete filesystem specification
used by the NeoBench kernel and tools.
EOF

FILES=(
00_overview
01_disk_layout
02_superblock
03_block_groups
04_block_bitmap
05_inode_bitmap
06_inode_table
07_inode_format
08_extent_format
09_directory_format
10_file_layout
11_permissions
12_links
13_journal
14_transactions
15_block_cache
16_allocator
17_mount
18_unmount
19_recovery
20_checksums
21_compression
22_encryption
23_snapshots
24_kernel_api
25_vfs_api
26_userspace_api
27_formatter
28_fsck
29_debug_tools
30_feature_flags
31_versioning
32_limits
33_boot_support
34_future
appendix
glossary
)

for FILE in "${FILES[@]}"
do
TITLE=$(echo "$FILE" | tr '_' ' ')

cat > "$DOCROOT/${FILE}.md" <<EOF
# $TITLE

Status:
Draft

Author:
NeoBench Project

---

## Purpose

Describe this subsystem.

---

## Design

Documentation goes here.

---

## Structures

To be completed.

---

## Algorithms

To be completed.

---

## Error Handling

To be completed.

---

## Future Improvements

To be completed.
EOF

done

cat > "$DOCROOT/SUMMARY.md" <<EOF
# NBFS Specification

| Chapter | Description |
|----------|-------------|
| 00 | Overview |
| 01 | Disk Layout |
| 02 | Superblock |
| 03 | Block Groups |
| 04 | Block Bitmap |
| 05 | Inode Bitmap |
| 06 | Inode Table |
| 07 | Inode Format |
| 08 | Extents |
| 09 | Directories |
| 10 | File Layout |
| 11 | Permissions |
| 12 | Links |
| 13 | Journal |
| 14 | Transactions |
| 15 | Block Cache |
| 16 | Allocator |
| 17 | Mount |
| 18 | Unmount |
| 19 | Recovery |
| 20 | Checksums |
| 21 | Compression |
| 22 | Encryption |
| 23 | Snapshots |
| 24 | Kernel API |
| 25 | VFS API |
| 26 | Userspace API |
| 27 | mkfs.nbfs |
| 28 | fsck.nbfs |
| 29 | Debug Tools |
| 30 | Feature Flags |
| 31 | Versioning |
| 32 | Limits |
| 33 | Boot Support |
| 34 | Future |
| A | Appendix |
| G | Glossary |
EOF

echo
echo "========================================"
echo " NBFS Documentation Created"
echo "========================================"
echo
echo "Location:"
echo "  $DOCROOT"
echo
echo "Documents created: ${#FILES[@]} + README + SUMMARY"
