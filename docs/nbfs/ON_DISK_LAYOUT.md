# NBFS On-Disk Layout
Version: 1.0 Draft

---

# Overview

NBFS stores all metadata at fixed locations during version 1.

Block size:
4096 bytes

All values are little-endian.

---

# Disk Layout

Offset (Blocks)

0
    Boot Block

1
    Primary Superblock

2
    Backup Superblock

3-18
    Journal

19-26
    Block Bitmap

27-34
    Inode Bitmap

35-98
    Inode Table

99
    Root Directory

100+
    File Data

---

# Boot Block

Size:
4096 bytes

Structure

Offset      Size

0x0000      Boot Loader

0x0FF0      Boot Signature

0x0FFC      CRC32

---

# Superblock

Block 1

Size

4096 bytes

Structure

Offset      Size      Description

0x0000      4         Magic ("NBFS")

0x0004      4         Version

0x0008      4         Block Size

0x000C      4         Flags

0x0010      8         Total Blocks

0x0018      8         Free Blocks

0x0020      8         Total Inodes

0x0028      8         Free Inodes

0x0030      8         Root Inode

0x0038      8         Journal Start

0x0040      8         Journal Length

0x0048      8         Block Bitmap

0x0050      8         Inode Bitmap

0x0058      8         Inode Table

0x0060      8         Data Start

0x0068      64        Volume Name

0x00A8      4         CRC32

---

# Inode

Size

256 bytes

Fields

Inode Number

Mode

UID

GID

Flags

Size

Blocks

Created

Modified

Accessed

12 Direct Extents

Indirect Extent

CRC32

---

# Directory Entry

Variable length

Structure

Inode

Record Length

Name Length

Type

Filename

Padding

---

# Extent

Extent Number

Starting Block

Length

Flags

---

# Journal Record

Transaction ID

Sequence Number

Timestamp

Payload Length

Payload

CRC32

---

# Block Bitmap

One bit per block.

0 = Free

1 = Used

---

# Inode Bitmap

One bit per inode.

0 = Free

1 = Used

---

# Reserved Areas

All unused bytes must be zero.

---

# Alignment

Metadata

8-byte aligned.

Directory entries

4-byte aligned.

---

# Checksums

CRC32

Superblock

Inodes

Journal

---

# Reserved Feature Flags

Compression

Encryption

Snapshots

Copy-on-write

Deduplication

Online Resize

64-bit Inodes

Extended Attributes
