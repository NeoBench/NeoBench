# NeoBench File System (NBFS)

Version: 1.0 Draft

---

# Goals

NBFS is a modern journaling filesystem designed specifically for the NeoBench operating system.

Goals:

- Extremely fast mounting
- Extent-based allocation
- 64-bit block numbers
- Journaling
- Metadata CRC protection
- Large file support
- Low fragmentation
- Simple recovery
- Optimised for SSD and SATA
- Portable across all NeoBench architectures

---

# Endianness

All on-disk structures are Little Endian.

---

# Block Size

Default:

4096 bytes

Supported:

1024
2048
4096
8192
16384

---

# Boot Block

Block 0

Reserved.

Contains:

- Boot loader
- Boot signature
- Reserved space

---

# Primary Superblock

Block 1

Contains filesystem metadata.

---

# Backup Superblock

Block 2

Mirror of the primary superblock.

---

# Journal

Immediately follows the backup superblock.

Used for:

- metadata updates
- recovery
- transactions

---

# Block Bitmap

Tracks used/free blocks.

---

# Inode Bitmap

Tracks inode allocation.

---

# Inode Table

Contains every inode.

Each inode is fixed size.

---

# Root Directory

Always inode 1.

---

# Data Area

Contains file contents.

---

# File Allocation

Extent based.

Each inode stores extents instead of block lists.

---

# Maximum File Size

16 EiB theoretical

Implementation limit depends on block size.

---

# Maximum Filesystem Size

16 EiB

---

# Filenames

UTF-8

Maximum:

255 bytes

---

# Timestamps

Stored as

64-bit Unix time.

---

# Permissions

POSIX compatible.

---

# Links

Hard links

Supported.

Symbolic links

Supported.

---

# CRC

CRC32 protects

- Superblock
- Inodes
- Journal

---

# Future Features

Compression

Encryption

Snapshots

Checksums per extent

Deduplication

Copy-on-write

Online resize
