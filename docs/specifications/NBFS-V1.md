NBFS Version 1 Specification
Design Goals
Native NeoBench filesystem
64-bit addressing
Journaling
Extent-based allocation
Fast boot
Fast directory lookup
Metadata checksums
Crash recovery
Support for files larger than 4 GB
Multiple partitions
Future snapshot support

Block Sizes

Supported:

512
1024
2048
4096   (default)
8192
16384

Default:

4096 bytes
Disk Layout
LBA 0
──────────────────────────────
Boot Sector

LBA 1
──────────────────────────────
Primary Superblock

LBA 2
──────────────────────────────
Backup Superblock

LBA 3
──────────────────────────────
Journal

LBA 64
──────────────────────────────
Block Bitmap

──────────────────────────────
Inode Bitmap

──────────────────────────────
Inode Table

──────────────────────────────
Root Directory

──────────────────────────────
Data Area
Superblock
typedef struct
{
    uint64_t magic;

    uint32_t version;

    uint32_t block_size;

    uint64_t total_blocks;

    uint64_t free_blocks;

    uint64_t inode_count;

    uint64_t free_inodes;

    uint64_t root_inode;

    uint64_t journal_start;

    uint64_t journal_length;

    uint64_t checksum;

} nbfs_superblock_t;
Inode
typedef struct
{
    uint64_t inode;

    uint64_t parent;

    uint64_t size;

    uint64_t blocks;

    uint64_t created;

    uint64_t modified;

    uint64_t accessed;

    uint32_t uid;

    uint32_t gid;

    uint32_t permissions;

    uint32_t flags;

    uint64_t extents[12];

} nbfs_inode_t;
Extent
Start Block

Length

No block chains.

Directory Entry
typedef struct
{
    uint64_t inode;

    uint16_t type;

    uint16_t name_length;

    char name[256];

} nbfs_directory_entry_t;
File Types
Regular File
Directory
Symbolic Link
Block Device
Character Device
Pipe
Socket
Permissions
Owner

Group

Other

Read
Write
Execute
Journaling

Every metadata change follows this sequence:

Transaction Begin

↓

Journal Write

↓

Disk Update

↓

Journal Commit

This ensures the filesystem can recover after an unexpected shutdown.

Checksums

Protect:

Superblock
Inodes
Directory blocks
Journal blocks

CRC32 is simple to implement; stronger checksums such as CRC64 or xxHash could be considered later.

Compression

Optional, per file:

None

LZ4

Reserved
Encryption

Future feature:

AES-256

Store encryption metadata in the inode rather than encrypting the whole volume.

Mount Process
Read Superblock

↓

Verify Magic

↓

Verify Version

↓

Verify Checksum

↓

Replay Journal

↓

Load Root Inode

↓

Mount Filesystem
Directory Indexing

For large directories, use a B+ tree instead of a linear list. This keeps lookup performance acceptable even with thousands of files.

Maximum Limits
Item	Value
Max File Size	16 EiB
Max Files	2⁶⁴
Max Filename	255 bytes
Max Path	4096 bytes
Max Block Size	16 KB
Max Volume	16 EiB
