#include "../include/neobench.h"
#include "../include/types.h"


namespace neo {
namespace neofs {

static constexpr uint32_t INODE_SIZE = 256; // or 128 if you want denser FS

/* ========================================================================
 * Compile-time geometry
 * ======================================================================== */

static constexpr uint32_t BLOCK_SIZE        = 4096;
static constexpr uint32_t BLOCK_SHIFT       = 12;           /* log2(4096) */
static constexpr uint32_t SECTOR_SIZE       = 512;
static constexpr uint32_t SECTORS_PER_BLOCK = BLOCK_SIZE / SECTOR_SIZE;  /* 8 */
static constexpr uint32_t INODES_PER_BLOCK  = BLOCK_SIZE / INODE_SIZE;   /* 16 */
static constexpr uint32_t MAX_INLINE_BYTES  = 128;          /* inline file data */
static constexpr uint32_t MAX_EXTENTS_INLINE = 6;           /* extents in inode */
static constexpr uint32_t MAX_NAME_LEN      = 255;

/* Fixed on-disk block addresses */
static constexpr uint32_t SB_PRIMARY        = 0;
static constexpr uint32_t SB_MIRROR         = 1;
static constexpr uint32_t INODE_BITMAP_START = 2;           /* may span multiple blocks */

/* Reserved inode numbers */
static constexpr uint32_t INO_INVALID       = 0;
static constexpr uint32_t INO_ROOT          = 1;
static constexpr uint32_t INO_JOURNAL       = 2;
static constexpr uint32_t INO_BADBLOCKS     = 3;
static constexpr uint32_t INO_FIRST_USER    = 8;

/* File type flags (upper 4 bits of inode.mode) */
static constexpr uint16_t NFS_IFMT          = 0xF000;
static constexpr uint16_t NFS_IFREG         = 0x8000;       /* regular file */
static constexpr uint16_t NFS_IFDIR         = 0x4000;       /* directory */
static constexpr uint16_t NFS_IFLNK         = 0x2000;       /* symbolic link */
static constexpr uint16_t NFS_IFBLK         = 0x6000;       /* block device */
static constexpr uint16_t NFS_IFCHR         = 0x2000;       /* character device */
static constexpr uint16_t NFS_IFIFO         = 0x1000;       /* pipe */

/* Permission bits (lower 12 bits of mode, Unix-style) */
static constexpr uint16_t NFS_IRUSR         = 0x0100;
static constexpr uint16_t NFS_IWUSR         = 0x0080;
static constexpr uint16_t NFS_IXUSR         = 0x0040;
static constexpr uint16_t NFS_IRGRP         = 0x0020;
static constexpr uint16_t NFS_IWGRP         = 0x0010;
static constexpr uint16_t NFS_IXGRP         = 0x0008;
static constexpr uint16_t NFS_IROTH         = 0x0004;
static constexpr uint16_t NFS_IWOTH         = 0x0002;
static constexpr uint16_t NFS_IXOTH         = 0x0001;

/* Inode flags */
static constexpr uint32_t NFS_FL_INLINE     = (1u << 0);    /* data stored in inode */
static constexpr uint32_t NFS_FL_IMMUTABLE  = (1u << 1);    /* cannot be modified */
static constexpr uint32_t NFS_FL_APPEND     = (1u << 2);    /* append-only */
static constexpr uint32_t NFS_FL_NOATIME   = (1u << 3);    /* don't update atime */
static constexpr uint32_t NFS_FL_SYNC       = (1u << 4);    /* synchronous writes */
static constexpr uint32_t NFS_FL_EXTENTS    = (1u << 5);    /* extent overflow block */
static constexpr uint32_t NFS_FL_DELETED    = (1u << 31);   /* deleted, awaiting GC */

/* Journal entry types */
static constexpr uint32_t JNL_MAGIC         = 0x4E4A524EUL; /* "NRJN" */
static constexpr uint32_t JNL_BEGIN         = 0x42454749UL; /* "BEGI" */
static constexpr uint32_t JNL_COMMIT        = 0x434F4D54UL; /* "COMT" */
static constexpr uint32_t JNL_BLOCK         = 0x424C4B20UL; /* "BLK " */
static constexpr uint32_t JNL_END_MARK      = 0xFFFFFFFFUL;

/* Superblock magic and version */
static constexpr uint32_t NEOFS_MAGIC       = 0x4E454F46UL; /* "NEOF" */
static constexpr uint32_t NEOFS_VERSION     = 0x00010000UL; /* 1.0.0 */

/* Directory entry type codes (cached in direntry, matches inode type) */
static constexpr uint8_t  DT_UNKNOWN        = 0;
static constexpr uint8_t  DT_FIFO           = 1;
static constexpr uint8_t  DT_CHR            = 2;
static constexpr uint8_t  DT_DIR            = 4;
static constexpr uint8_t  DT_BLK            = 6;
static constexpr uint8_t  DT_REG            = 8;
static constexpr uint8_t  DT_LNK            = 10;
static constexpr uint8_t  DT_SOCK           = 12;

/* CRC32C polynomial (Castagnoli) */
static constexpr uint32_t CRC32C_POLY       = 0x82F63B78UL;

/* ========================================================================
 * On-disk structures
 *
 * All multi-byte integers are big-endian (native 68k byte order).
 * No byte swapping needed on Amiga - written directly to disk.
 * ======================================================================== */

/*
 * Superblock - 512 bytes, stored at block 0 (primary) and block 1 (mirror).
 * Padded to exactly 512 bytes so it aligns cleanly within a 4KB block.
 */
struct __attribute__((packed)) Superblock {
    uint32_t magic;             /* NEOFS_MAGIC */
    uint32_t version;           /* NEOFS_VERSION */
    uint32_t block_size;        /* Always 4096 */
    uint32_t total_blocks;      /* Total 4KB blocks in volume */
    uint32_t free_blocks;       /* Free data blocks */
    uint32_t total_inodes;      /* Total inodes allocated */
    uint32_t free_inodes;       /* Free inodes */
    uint32_t inode_bitmap_start;/* First block of inode bitmap */
    uint32_t inode_bitmap_blocks;/* Number of inode bitmap blocks */
    uint32_t block_bitmap_start;/* First block of block allocation bitmap */
    uint32_t block_bitmap_blocks;/* Number of block bitmap blocks */
    uint32_t inode_table_start; /* First block of inode table */
    uint32_t inode_table_blocks;/* Number of inode table blocks */
    uint32_t data_start;        /* First data block */
    uint32_t journal_blocks;    /* Number of blocks in journal ring */
    uint32_t journal_head;      /* Journal write head (block index in ring) */
    uint32_t journal_tail;      /* Journal replay tail */
    uint32_t journal_txn_id;    /* Next transaction ID */
    uint32_t mount_count;       /* Number of times mounted */
    uint32_t max_mount_count;   /* fsck recommended after this many mounts */
    uint32_t state;             /* 0=clean, 1=dirty, 2=error */
    uint32_t last_check_time;   /* Last fsck time (seconds since 1978-01-01) */
    uint32_t last_mount_time;
    uint32_t last_write_time;
    uint32_t creator_os;        /* 0 = NeoBench */
    uint32_t uuid_hi;           /* Volume UUID high 32 bits */
    uint32_t uuid_lo;           /* Volume UUID low 32 bits */
    uint32_t crc32;             /* CRC32C of this struct (with crc32=0) */
    char     volume_name[32];   /* Volume label, NUL-terminated */
    char     last_mounted[64];  /* Last mount path (informational) */
    uint8_t  reserved[304];     /* Pad to 512 bytes */
};

static_assert(sizeof(Superblock) == 512, "Superblock must be 512 bytes");

/*
 * Extent descriptor - 12 bytes
 * Describes a contiguous run of blocks on disk.
 */
struct __attribute__((packed)) Extent {
    uint32_t logical;           /* First logical block in file this covers */
    uint32_t physical;          /* First physical block on disk */
    uint16_t length;            /* Number of 4KB blocks (max 65535 = INODE_SIZEMB) */
    uint16_t flags;             /* Per-extent flags */
};

static constexpr uint16_t EXT_FL_UNWRITTEN = 0x0001; /* Allocated but not written */
static constexpr uint16_t EXT_FL_ENCRYPTED = 0x0002; /* Encrypted (future) */

/*
 * Extent overflow block header - for files with more extents than fit inline.
 * Stored at the first block pointed to by inode.overflow_block.
 * Can chain to further overflow blocks via overflow_next.
 */
struct __attribute__((packed)) ExtentBlock {
    uint32_t magic;             /* 0x4E455854 "NEXT" */
    uint32_t inode;             /* Owner inode number */
    uint32_t overflow_next;     /* Next overflow block (0 = none) */
    uint16_t entry_count;       /* Number of valid Extent entries */
    uint16_t crc16;             /* CRC16 of this block */
    Extent   extents[340];      /* (4096 - 16) / 12 = 340, use 337 for alignment */
};

static_assert(sizeof(ExtentBlock) == 4096, "ExtentBlock must be 4096 bytes");

/*
 * Timestamp - 8 bytes
 */
struct __attribute__((packed)) Timestamp {
    uint32_t seconds;           /* Seconds since 1978-01-01 00:00:00 (Amiga epoch) */
    uint32_t nanoseconds;       /* Nanosecond fraction */
};

/*
 * Inode - INODE_SIZE bytes
 *
 * For regular files: up to MAX_EXTENTS_INLINE extents inline, or inline data
 * For directories: extents pointing to directory data blocks
 * For symlinks < 128 bytes: inline target path in data.inline_data
 */
struct __attribute__((packed)) Inode {
    uint16_t  mode;             /* File type + Unix permission bits */
    uint16_t  uid;              /* Owner user ID */
    uint16_t  gid;              /* Owner group ID */
    uint16_t  link_count;       /* Hard link count */
    uint32_t  flags;            /* NFS_FL_* flags */
    uint64_t  size;             /* File size in bytes */
    Timestamp atime;            /* Last access */
    Timestamp mtime;            /* Last modification */
    Timestamp ctime;            /* Last status change */
    Timestamp crtime;           /* Creation time */
    uint32_t  block_count;      /* 512-byte blocks allocated */
    uint32_t  overflow_block;   /* First extent overflow block (0 = none) */
    uint32_t  xattr_block;      /* Extended attributes block (0 = none) */
    uint32_t  generation;       /* Inode generation counter */
    uint32_t  crc32;            /* CRC32C of this inode (with crc32=0) */

    /* 80 bytes of data area:
     *   If NFS_FL_INLINE: first 128 bytes of file content
     *   Otherwise: up to 6 Extent descriptors (6 × 12 = 72 bytes)
     *              followed by 8 bytes reserved
     */
    union {
        uint8_t  inline_data[128];
        struct {
            Extent   extents[MAX_EXTENTS_INLINE];  /* 6 × 12 = 72 bytes */
            uint8_t  _pad[8];
        } tree;
    } data;

    uint8_t  reserved[56];      /* Pad to INODE_SIZE bytes */
};

static_assert(sizeof(Inode) == INODE_SIZE, "Inode must be INODE_SIZE bytes");

/*
 * Directory entry - variable length within a directory data block.
 * Entries are packed sequentially; rec_len is used to skip to the next.
 * rec_len is always rounded up to a 4-byte boundary.
 * A rec_len of 0 means end-of-used-space in this block.
 */
struct __attribute__((packed)) DirEntry {
    uint32_t  inode;            /* Inode number (0 = deleted/unused entry) */
    uint16_t  rec_len;          /* Byte length of this record (aligned to 4) */
    uint8_t   name_len;         /* Length of name in bytes (no NUL) */
    uint8_t   file_type;        /* DT_* type code (cached from inode) */
    char      name[0];          /* Name bytes (NOT NUL-terminated on disk) */
};

/* Minimum DirEntry size (inode=0 tombstone entry) */
static constexpr uint16_t DIRENT_MIN_REC = 8;

/* Compute rec_len for a given name_len: round up to 4 bytes */
static inline uint16_t dirent_rec_len(uint8_t name_len)
{
    return (uint16_t)(((uint32_t)sizeof(DirEntry) + name_len + 3u) & ~3u);
}

/*
 * Journal entry header - 32 bytes, stored at start of each journal block
 */
struct __attribute__((packed)) JournalEntry {
    uint32_t magic;             /* JNL_MAGIC */
    uint32_t type;              /* JNL_BEGIN / JNL_COMMIT / JNL_BLOCK */
    uint32_t txn_id;            /* Transaction this belongs to */
    uint32_t target_block;      /* Physical block number (JNL_BLOCK only) */
    uint32_t timestamp;         /* Seconds since Amiga epoch */
    uint32_t sequence;          /* Sequence number within transaction */
    uint32_t data_crc32;        /* CRC32C of following data block (JNL_BLOCK) */
    uint32_t header_crc32;      /* CRC32C of this header (with header_crc32=0) */
};

static_assert(sizeof(JournalEntry) == 32, "JournalEntry must be 32 bytes");

/* ========================================================================
 * CRC32C - software implementation (Castagnoli polynomial)
 * ======================================================================== */

static uint32_t crc32c_table[256];
static bool     crc32c_ready = false;

static void crc32c_init(void)
{
    for (uint32_t i = 0; i < INODE_SIZE; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? CRC32C_POLY : 0);
        crc32c_table[i] = crc;
    }
    crc32c_ready = true;
}

static uint32_t crc32c(const void *data, uint32_t len, uint32_t seed = 0xFFFFFFFFUL)
{
    if (!crc32c_ready) crc32c_init();
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = seed;
    for (uint32_t i = 0; i < len; i++)
        crc = crc32c_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

/* ========================================================================
 * I/O callbacks and volume state
 * ======================================================================== */

typedef bool (*NeoReadFunc) (uint32_t block_4k, void *buf);
typedef bool (*NeoWriteFunc)(uint32_t block_4k, const void *buf);

/* Scratch I/O buffer - one 4KB block */
static uint8_t  io_buf[BLOCK_SIZE]  __attribute__((aligned(4)));
static uint8_t  io_buf2[BLOCK_SIZE] __attribute__((aligned(4)));

static NeoReadFunc   read_fn      = nullptr;
static NeoWriteFunc  write_fn     = nullptr;
static uint32_t      vol_offset   = 0;    /* First 4KB block of this volume */
static bool          mounted      = false;
static bool          read_only    = false;

static Superblock sb;

/* Current open transaction */
static uint32_t txn_id     = 0;
static uint32_t txn_seq    = 0;
static bool     txn_active = false;

/* ========================================================================
 * Block I/O - translates 4KB NeoFS blocks to physical disk sectors
 *
 * The read/write callbacks supplied by the caller work in 4KB units.
 * vol_offset is the first 4KB block of this NeoFS volume.
 * ======================================================================== */

static bool blk_read(uint32_t block)
{
    if (!read_fn) return false;
    return read_fn(vol_offset + block, io_buf);
}

static bool blk_write(uint32_t block)
{
    if (!write_fn || read_only) return false;
    return write_fn(vol_offset + block, io_buf);
}

static bool blk_read_to(uint32_t block, void *buf)
{
    if (!read_fn) return false;
    return read_fn(vol_offset + block, buf);
}

static bool blk_write_from(uint32_t block, const void *buf)
{
    if (!write_fn || read_only) return false;
    return write_fn(vol_offset + block, buf);
}

/* ========================================================================
 * Superblock operations
 * ======================================================================== */

static bool sb_verify(const Superblock *s)
{
    if (s->magic != NEOFS_MAGIC) return false;
    if ((s->version >> 16) != 1)  return false;   /* Major version check */
    if (s->block_size != BLOCK_SIZE) return false;

    uint32_t saved = s->crc32;
    const_cast<Superblock *>(s)->crc32 = 0;
    uint32_t computed = crc32c(s, sizeof(Superblock));
    const_cast<Superblock *>(s)->crc32 = saved;
    return computed == saved;
}

static bool sb_read(void)
{
    /* Try primary superblock */
    if (blk_read_to(SB_PRIMARY, &sb) && sb_verify(&sb))
        return true;
    /* Fall back to mirror */
    if (blk_read_to(SB_MIRROR, &sb) && sb_verify(&sb))
        return true;
    return false;
}

static bool sb_write(void)
{
    if (read_only) return false;
    sb.last_write_time = 0; /* Caller sets timestamp */
    sb.crc32 = 0;
    sb.crc32 = crc32c(&sb, sizeof(Superblock));

    uint8_t block[BLOCK_SIZE];
    __builtin_memset(block, 0, BLOCK_SIZE);
    __builtin_memcpy(block, &sb, sizeof(Superblock));

    if (!blk_write_from(SB_PRIMARY, block)) return false;
    if (!blk_write_from(SB_MIRROR,  block)) return false;
    return true;
}

/* ========================================================================
 * Inode operations
 * ======================================================================== */

static bool inode_read(uint32_t ino, Inode *out)
{
    if (ino == INO_INVALID || ino > sb.total_inodes) return false;

    uint32_t idx    = ino - 1;                            /* 0-based index */
    uint32_t blk    = sb.inode_table_start + (idx / INODES_PER_BLOCK);
    uint32_t offset = (idx % INODES_PER_BLOCK) * INODE_SIZE;

    if (!blk_read(blk)) return false;

    __builtin_memcpy(out, io_buf + offset, sizeof(Inode));

    /* Verify inode CRC */
    uint32_t saved = out->crc32;
    out->crc32 = 0;
    uint32_t computed = crc32c(out, sizeof(Inode));
    out->crc32 = saved;
    return computed == saved;
}

static bool inode_write(uint32_t ino, const Inode *in)
{
    if (read_only || ino == INO_INVALID) return false;
    if (ino > sb.total_inodes) return false;

    uint32_t idx    = ino - 1;
    uint32_t blk    = sb.inode_table_start + (idx / INODES_PER_BLOCK);
    uint32_t offset = (idx % INODES_PER_BLOCK) * INODE_SIZE;

    if (!blk_read(blk)) return false;

    Inode *dest = (Inode *)(io_buf + offset);
    __builtin_memcpy(dest, in, sizeof(Inode));
    dest->crc32 = 0;
    dest->crc32 = crc32c(dest, sizeof(Inode));

    return blk_write(blk);
}

/* ========================================================================
 * Block allocation bitmap
 *
 * One bit per 4KB block.  Bit 0 of byte 0 = block 0 (the primary SB).
 * Pre-allocated metadata blocks are already set to 1 at format time.
 * ======================================================================== */

static uint32_t alloc_block(void)
{
    if (read_only || sb.free_blocks == 0) return 0;

    uint32_t bmp_blocks = sb.block_bitmap_blocks;

    for (uint32_t bmp_blk = 0; bmp_blk < bmp_blocks; bmp_blk++) {
        if (!blk_read(sb.block_bitmap_start + bmp_blk)) continue;

        uint8_t *bmp = io_buf;
        for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
            if (bmp[byte] == 0xFF) continue;
            for (int bit = 0; bit < 8; bit++) {
                if (!(bmp[byte] & (1u << bit))) {
                    bmp[byte] |= (1u << bit);
                    blk_write(sb.block_bitmap_start + bmp_blk);
                    sb.free_blocks--;
                    /* Physical block = bit position in global bitmap */
                    return sb.data_start +
                           bmp_blk * (BLOCK_SIZE * 8) +
                           byte * 8 + (uint32_t)bit;
                }
            }
        }
    }
    return 0; /* No free blocks */
}

static bool free_block(uint32_t phys_block)
{
    if (read_only || phys_block < sb.data_start) return false;

    uint32_t rel  = phys_block - sb.data_start;
    uint32_t bmp_blk_idx = rel / (BLOCK_SIZE * 8);
    uint32_t bit_in_bmp  = rel % (BLOCK_SIZE * 8);
    uint32_t byte_idx    = bit_in_bmp / 8;
    uint32_t bit_idx     = bit_in_bmp % 8;

    if (bmp_blk_idx >= sb.block_bitmap_blocks) return false;

    if (!blk_read(sb.block_bitmap_start + bmp_blk_idx)) return false;

    io_buf[byte_idx] &= ~(1u << bit_idx);
    blk_write(sb.block_bitmap_start + bmp_blk_idx);
    sb.free_blocks++;
    return true;
}

/* ========================================================================
 * Inode allocation bitmap
 * ======================================================================== */

static uint32_t alloc_inode(void)
{
    if (read_only || sb.free_inodes == 0) return INO_INVALID;

    for (uint32_t bmp_blk = 0; bmp_blk < sb.inode_bitmap_blocks; bmp_blk++) {
        if (!blk_read(sb.inode_bitmap_start + bmp_blk)) continue;

        uint8_t *bmp = io_buf;
        for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
            if (bmp[byte] == 0xFF) continue;
            for (int bit = 0; bit < 8; bit++) {
                if (!(bmp[byte] & (1u << bit))) {
                    bmp[byte] |= (1u << bit);
                    blk_write(sb.inode_bitmap_start + bmp_blk);
                    sb.free_inodes--;
                    /* Inode numbers start at 1 */
                    return bmp_blk * (BLOCK_SIZE * 8) +
                           byte * 8 + (uint32_t)bit + 1;
                }
            }
        }
    }
    return INO_INVALID;
}

static bool free_inode(uint32_t ino)
{
    if (read_only || ino == INO_INVALID) return false;

    uint32_t idx  = ino - 1;
    uint32_t bblk = idx / (BLOCK_SIZE * 8);
    uint32_t boff = (idx % (BLOCK_SIZE * 8)) / 8;
    uint32_t bbit = idx % 8;

    if (bblk >= sb.inode_bitmap_blocks) return false;

    if (!blk_read(sb.inode_bitmap_start + bblk)) return false;
    io_buf[boff] &= ~(1u << bbit);
    blk_write(sb.inode_bitmap_start + bblk);
    sb.free_inodes++;
    return true;
}

/* ========================================================================
 * Extent tree
 *
 * An inode holds up to MAX_EXTENTS_INLINE extents inline.
 * If more are needed, overflow_block points to an ExtentBlock on disk.
 * ExtentBlocks can chain via overflow_next for very fragmented files.
 *
 * For NeoBench usage (typical file sizes < 1GB on Amiga partitions),
 * inline extents are sufficient for most files.  A file stored in
 * one contiguous run needs only one extent regardless of its size.
 * ======================================================================== */

/* Find the physical block for a given logical block in a file */
static bool extent_lookup(const Inode *ino, uint32_t logical, uint32_t *physical)
{
    if (ino->flags & NFS_FL_INLINE) return false; /* Inline data has no extents */

    /* Search inline extents */
    const Extent *exts = ino->data.tree.extents;
    uint16_t count = 0;

    /* Count valid inline extents (length > 0) */
    for (int i = 0; i < (int)MAX_EXTENTS_INLINE; i++) {
        if (exts[i].length == 0) break;
        count++;
    }

    for (uint16_t i = 0; i < count; i++) {
        uint32_t start = exts[i].logical;
        uint32_t end   = start + exts[i].length;
        if (logical >= start && logical < end) {
            *physical = exts[i].physical + (logical - start);
            return true;
        }
    }

    /* Search overflow blocks */
    uint32_t ovf = ino->overflow_block;
    while (ovf != 0) {
        ExtentBlock eb;
        if (!blk_read_to(ovf, &eb)) return false;
        if (eb.magic != 0x4E455854UL) return false; /* "NEXT" */

        for (uint16_t i = 0; i < eb.entry_count; i++) {
            uint32_t start = eb.extents[i].logical;
            uint32_t end   = start + eb.extents[i].length;
            if (logical >= start && logical < end) {
                *physical = eb.extents[i].physical + (logical - start);
                return true;
            }
        }
        ovf = eb.overflow_next;
    }

    return false; /* Block not mapped */
}

/* Add a new extent to a file's extent tree */
static bool extent_append(Inode *ino, uint32_t logical, uint32_t physical,
                           uint16_t length)
{
    if (read_only) return false;

    /* Try to extend the last inline extent if contiguous */
    Extent *exts = ino->data.tree.extents;
    for (int i = MAX_EXTENTS_INLINE - 1; i >= 0; i--) {
        if (exts[i].length == 0) continue;
        /* Check if this new extent immediately follows the last one */
        uint32_t last_log = exts[i].logical + exts[i].length;
        uint32_t last_phy = exts[i].physical + exts[i].length;
        if (last_log == logical && last_phy == physical) {
            /* Extend: check for uint16 overflow */
            if ((uint32_t)exts[i].length + length <= 0xFFFF) {
                exts[i].length = (uint16_t)(exts[i].length + length);
                return true;
            }
        }
        break; /* Last extent found */
    }

    /* Find first empty inline slot */
    for (int i = 0; i < (int)MAX_EXTENTS_INLINE; i++) {
        if (exts[i].length == 0) {
            exts[i].logical  = logical;
            exts[i].physical = physical;
            exts[i].length   = length;
            exts[i].flags    = 0;
            return true;
        }
    }

    /* Inline slots full - need overflow block */
    if (ino->overflow_block == 0) {
        /* Allocate a new overflow block */
        uint32_t ovf_blk = alloc_block();
        if (!ovf_blk) return false;

        ExtentBlock eb;
        __builtin_memset(&eb, 0, sizeof(eb));
        eb.magic       = 0x4E455854UL;
        eb.inode       = 0; /* Caller sets if needed */
        eb.overflow_next = 0;
        eb.extents[0].logical  = logical;
        eb.extents[0].physical = physical;
        eb.extents[0].length   = length;
        eb.entry_count = 1;

        if (!blk_write_from(ovf_blk, &eb)) {
            free_block(ovf_blk);
            return false;
        }
        ino->overflow_block = ovf_blk;
        ino->flags |= NFS_FL_EXTENTS;
        return true;
    }

    /* Walk to last overflow block and append there */
    uint32_t ovf = ino->overflow_block;
    ExtentBlock eb;
    while (true) {
        if (!blk_read_to(ovf, &eb)) return false;
        if (eb.overflow_next == 0) break;
        ovf = eb.overflow_next;
    }

    if (eb.entry_count < 337) {
        uint16_t e = eb.entry_count;
        eb.extents[e].logical  = logical;
        eb.extents[e].physical = physical;
        eb.extents[e].length   = length;
        eb.extents[e].flags    = 0;
        eb.entry_count++;
        return blk_write_from(ovf, &eb);
    }

    /* This overflow block is full; chain a new one */
    uint32_t new_ovf = alloc_block();
    if (!new_ovf) return false;

    eb.overflow_next = new_ovf;
    if (!blk_write_from(ovf, &eb)) {
        free_block(new_ovf);
        return false;
    }

    ExtentBlock new_eb;
    __builtin_memset(&new_eb, 0, sizeof(new_eb));
    new_eb.magic   = 0x4E455854UL;
    new_eb.extents[0].logical  = logical;
    new_eb.extents[0].physical = physical;
    new_eb.extents[0].length   = length;
    new_eb.entry_count = 1;
    return blk_write_from(new_ovf, &new_eb);
}

/* ========================================================================
 * Journal (write-ahead log)
 *
 * The journal occupies a contiguous ring of blocks immediately after
 * the inode table.  Block count is stored in sb.journal_blocks.
 *
 * Each journalled metadata write uses two consecutive journal slots:
 *   Slot N:   JournalEntry header
 *   Slot N+1: Full copy of the 4KB block being written
 *
 * On recovery, committed transactions are replayed (redo journal).
 * Uncommitted transactions are discarded.
 * ======================================================================== */

static uint32_t journal_start(void)
{
    /* Journal immediately follows the inode table */
    return sb.inode_table_start + sb.inode_table_blocks;
}

static bool journal_begin_txn(void)
{
    if (read_only || txn_active) return false;

    txn_id     = sb.journal_txn_id++;
    txn_seq    = 0;
    txn_active = true;

    JournalEntry jh;
    __builtin_memset(&jh, 0, sizeof(jh));
    jh.magic      = JNL_MAGIC;
    jh.type       = JNL_BEGIN;
    jh.txn_id     = txn_id;
    jh.sequence   = txn_seq++;
    jh.timestamp  = 0; /* TODO: RTC integration */
    jh.header_crc32 = 0;
    jh.header_crc32 = crc32c(&jh, sizeof(jh));

    uint32_t slot = journal_start() + (sb.journal_head % sb.journal_blocks);
    sb.journal_head++;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    __builtin_memcpy(io_buf, &jh, sizeof(jh));
    return blk_write(slot);
}

/* Log a block to the journal before modifying it */
static bool journal_log(uint32_t phys_block)
{
    if (!txn_active) return false;
    /* Check ring space: need 2 slots (header + data) */
    if (sb.journal_head + 1 >= sb.journal_tail + sb.journal_blocks)
        return false; /* Journal full */

    /* Read current block content */
    if (!blk_read_to(phys_block, io_buf2)) return false;

    /* Write journal header */
    JournalEntry jh;
    __builtin_memset(&jh, 0, sizeof(jh));
    jh.magic        = JNL_MAGIC;
    jh.type         = JNL_BLOCK;
    jh.txn_id       = txn_id;
    jh.target_block = phys_block + vol_offset;
    jh.sequence     = txn_seq++;
    jh.data_crc32   = crc32c(io_buf2, BLOCK_SIZE);
    jh.header_crc32 = 0;
    jh.header_crc32 = crc32c(&jh, sizeof(jh));

    uint32_t hdr_slot  = journal_start() + (sb.journal_head % sb.journal_blocks);
    uint32_t data_slot = journal_start() + ((sb.journal_head + 1) % sb.journal_blocks);
    sb.journal_head += 2;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    __builtin_memcpy(io_buf, &jh, sizeof(jh));
    if (!blk_write(hdr_slot))  return false;
    if (!blk_write_from(data_slot, io_buf2)) return false;

    return true;
}

static bool journal_commit(void)
{
    if (!txn_active) return false;

    JournalEntry jh;
    __builtin_memset(&jh, 0, sizeof(jh));
    jh.magic        = JNL_MAGIC;
    jh.type         = JNL_COMMIT;
    jh.txn_id       = txn_id;
    jh.sequence     = txn_seq++;
    jh.header_crc32 = 0;
    jh.header_crc32 = crc32c(&jh, sizeof(jh));

    uint32_t slot = journal_start() + (sb.journal_head % sb.journal_blocks);
    sb.journal_head++;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    __builtin_memcpy(io_buf, &jh, sizeof(jh));
    if (!blk_write(slot)) return false;

    txn_active = false;
    return sb_write();
}

static bool journal_replay(void)
{
    uint32_t jstart = journal_start();
    uint32_t pos    = sb.journal_tail;

    while (pos < sb.journal_head) {
        uint32_t slot = jstart + (pos % sb.journal_blocks);
        if (!blk_read(slot)) { pos++; continue; }

        JournalEntry *jh = (JournalEntry *)io_buf;

        /* Verify header CRC */
        uint32_t saved_crc = jh->header_crc32;
        jh->header_crc32 = 0;
        uint32_t computed = crc32c(jh, sizeof(*jh));
        jh->header_crc32 = saved_crc;
        if (computed != saved_crc) { pos++; continue; }

        if (jh->magic != JNL_MAGIC) { pos++; continue; }

        if (jh->type == JNL_BLOCK) {
            uint32_t target = jh->target_block;
            pos++;
            /* Next slot is the block data */
            uint32_t data_slot = jstart + (pos % sb.journal_blocks);
            if (blk_read_to(data_slot, io_buf2)) {
                /* Verify data CRC before applying */
                if (crc32c(io_buf2, BLOCK_SIZE) == jh->data_crc32) {
                    /* Re-apply to disk (redo) */
                    write_fn(target, io_buf2);
                }
            }
        }
        pos++;
    }

    sb.journal_tail = sb.journal_head;
    return sb_write();
}

/* ========================================================================
 * Case-insensitive filename comparison (Amiga convention)
 * ======================================================================== */

static bool name_match(const char *a, uint8_t alen,
                        const char *b, uint8_t blen)
{
    if (alen != blen) return false;
    for (uint8_t i = 0; i < alen; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return true;
}

static uint8_t str_len(const char *s)
{
    uint8_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ========================================================================
 * Directory operations
 * ======================================================================== */

/* Find an entry by name in a directory inode.
 * Returns inode number of the entry, or INO_INVALID if not found. */
static uint32_t dir_lookup(const Inode *dir, const char *name)
{
    if ((dir->mode & NFS_IFMT) != NFS_IFDIR) return INO_INVALID;

    uint8_t  nlen   = str_len(name);
    uint64_t size   = dir->size;
    uint32_t blocks = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < blocks; lb++) {
        uint32_t pb;
        if (!extent_lookup(dir, lb, &pb)) continue;
        if (!blk_read(pb)) continue;

        const uint8_t *ptr = io_buf;
        const uint8_t *end = io_buf + BLOCK_SIZE;

        while (ptr + DIRENT_MIN_REC <= end) {
            const DirEntry *de = (const DirEntry *)ptr;
            if (de->rec_len == 0) break;

            if (de->inode != INO_INVALID &&
                name_match(de->name, de->name_len, name, nlen)) {
                return de->inode;
            }

            ptr += de->rec_len;
        }
    }

    return INO_INVALID;
}

/* Add a directory entry */
static bool dir_add_entry(Inode *dir, uint32_t dir_ino,
                           uint32_t child_ino, const char *name,
                           uint8_t file_type)
{
    if (read_only) return false;

    uint8_t  nlen    = str_len(name);
    uint16_t needed  = dirent_rec_len(nlen);
    uint64_t size    = dir->size;
    uint32_t blocks  = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    /* Search existing blocks for space */
    for (uint32_t lb = 0; lb < blocks; lb++) {
        uint32_t pb;
        if (!extent_lookup(dir, lb, &pb)) continue;
        if (!blk_read(pb)) continue;

        uint8_t *ptr = io_buf;
        uint8_t *end = io_buf + BLOCK_SIZE;

        while (ptr + DIRENT_MIN_REC <= end) {
            DirEntry *de = (DirEntry *)ptr;
            if (de->rec_len == 0) {
                /* Reached end-of-used-space: enough room here? */
                uint32_t remaining = (uint32_t)(end - ptr);
                if (remaining >= needed) {
                    de->inode     = child_ino;
                    de->rec_len   = (uint16_t)remaining; /* Use all remaining */
                    de->name_len  = nlen;
                    de->file_type = file_type;
                    __builtin_memcpy(de->name, name, nlen);
                    return blk_write(pb);
                }
                break;
            }

            /* Check if this entry has slack space for our new entry */
            uint16_t actual_len = dirent_rec_len(de->name_len);
            uint16_t slack      = de->rec_len - actual_len;
            if (slack >= needed) {
                /* Split: shrink current entry, add new one after */
                uint16_t old_reclen = de->rec_len;
                de->rec_len = actual_len;

                DirEntry *new_de = (DirEntry *)(ptr + actual_len);
                new_de->inode     = child_ino;
                new_de->rec_len   = (uint16_t)(old_reclen - actual_len);
                new_de->name_len  = nlen;
                new_de->file_type = file_type;
                __builtin_memcpy(new_de->name, name, nlen);
                return blk_write(pb);
            }

            ptr += de->rec_len;
        }
    }

    /* No space in existing blocks; allocate a new directory data block */
    uint32_t new_pb = alloc_block();
    if (!new_pb) return false;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    DirEntry *de  = (DirEntry *)io_buf;
    de->inode     = child_ino;
    de->rec_len   = (uint16_t)(BLOCK_SIZE - DIRENT_MIN_REC); /* Leave sentinel at end */
    de->name_len  = nlen;
    de->file_type = file_type;
    __builtin_memcpy(de->name, name, nlen);

    if (!blk_write(new_pb)) {
        free_block(new_pb);
        return false;
    }

    /* Add extent to directory inode */
    uint32_t new_lb = blocks;
    if (!extent_append(dir, new_lb, new_pb, 1)) {
        free_block(new_pb);
        return false;
    }

    dir->size += BLOCK_SIZE;
    dir->block_count += SECTORS_PER_BLOCK;
    return inode_write(dir_ino, dir);
}

/* Remove a directory entry by name */
static bool dir_remove_entry(Inode *dir, uint32_t dir_ino, const char *name)
{
    if (read_only) return false;

    uint8_t  nlen   = str_len(name);
    uint32_t blocks = (uint32_t)((dir->size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < blocks; lb++) {
        uint32_t pb;
        if (!extent_lookup(dir, lb, &pb)) continue;
        if (!blk_read(pb)) continue;

        uint8_t *ptr  = io_buf;
        uint8_t *end  = io_buf + BLOCK_SIZE;
        DirEntry *prev = nullptr;

        while (ptr + DIRENT_MIN_REC <= end) {
            DirEntry *de = (DirEntry *)ptr;
            if (de->rec_len == 0) break;

            if (de->inode != INO_INVALID &&
                name_match(de->name, de->name_len, name, nlen)) {
                /* Found: merge rec_len into previous entry (or zero inode) */
                if (prev) {
                    prev->rec_len = (uint16_t)(prev->rec_len + de->rec_len);
                } else {
                    de->inode = INO_INVALID; /* Mark as deleted */
                }
                return blk_write(pb);
            }

            prev = de;
            ptr += de->rec_len;
        }
    }

    return false; /* Not found */
}

/* Check if a directory is empty (only . and .. if they exist, or truly empty) */
static bool dir_is_empty(const Inode *dir)
{
    uint32_t blocks = (uint32_t)((dir->size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < blocks; lb++) {
        uint32_t pb;
        if (!extent_lookup(dir, lb, &pb)) continue;
        if (!blk_read(pb)) continue;

        const uint8_t *ptr = io_buf;
        const uint8_t *end = io_buf + BLOCK_SIZE;

        while (ptr + DIRENT_MIN_REC <= end) {
            const DirEntry *de = (const DirEntry *)ptr;
            if (de->rec_len == 0) break;
            if (de->inode != INO_INVALID) {
                /* Not . or .. */
                if (!(de->name_len == 1 && de->name[0] == '.') &&
                    !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {
                    return false;
                }
            }
            ptr += de->rec_len;
        }
    }
    return true;
}

/* ========================================================================
 * Path resolution
 * ======================================================================== */

/* Resolve a path from a given starting inode.
 * Returns inode number of the target, or INO_INVALID on failure.
 * Follows symlinks up to 8 levels deep. */
static uint32_t path_resolve(uint32_t start_ino, const char *path,
                              int symlink_depth = 0)
{
    if (symlink_depth > 8) return INO_INVALID; /* Too many symlinks */

    uint32_t cur_ino = start_ino;

    /* Absolute path: start from root */
    if (path[0] == '/') {
        cur_ino = INO_ROOT;
        path++;
    }

    char component[MAX_NAME_LEN + 1];

    while (*path) {
        /* Skip leading slashes */
        while (*path == '/') path++;
        if (!*path) break;

        /* Extract next path component */
        int len = 0;
        while (path[len] && path[len] != '/' && len < (int)MAX_NAME_LEN) {
            component[len] = path[len];
            len++;
        }
        component[len] = '\0';
        path += len;

        /* Read current inode */
        Inode cur;
        if (!inode_read(cur_ino, &cur)) return INO_INVALID;

        /* Follow symlinks */
        if ((cur.mode & NFS_IFMT) == NFS_IFLNK) {
            if (cur.flags & NFS_FL_INLINE) {
                /* Inline symlink target */
                char target[MAX_INLINE_BYTES + 1];
                uint32_t tlen = (uint32_t)cur.size;
                if (tlen > MAX_INLINE_BYTES) tlen = MAX_INLINE_BYTES;
                __builtin_memcpy(target, cur.data.inline_data, tlen);
                target[tlen] = '\0';
                cur_ino = path_resolve(INO_ROOT, target, symlink_depth + 1);
                if (cur_ino == INO_INVALID) return INO_INVALID;
                /* Re-read the inode we resolved to */
                if (!inode_read(cur_ino, &cur)) return INO_INVALID;
            } else {
                return INO_INVALID; /* External symlink not supported in this path */
            }
        }

        if ((cur.mode & NFS_IFMT) != NFS_IFDIR) return INO_INVALID;

        cur_ino = dir_lookup(&cur, component);
        if (cur_ino == INO_INVALID) return INO_INVALID;
    }

    return cur_ino;
}

/* ========================================================================
 * Public API - stat / read / write
 * ======================================================================== */

/* Public stat structure */
struct NeoStat {
    uint32_t  ino;
    uint16_t  mode;
    uint16_t  uid;
    uint16_t  gid;
    uint16_t  link_count;
    uint64_t  size;
    uint32_t  block_count;   /* 512-byte units */
    Timestamp mtime;
    Timestamp ctime;
    Timestamp crtime;
    uint32_t  flags;
};

/* Public directory listing entry */
struct NeosDirEntry {
    uint32_t ino;
    uint8_t  type;
    char     name[MAX_NAME_LEN + 1];
};

/* ========================================================================
 * Mount / Unmount
 * ======================================================================== */

bool mount(NeoReadFunc reader, NeoWriteFunc writer,
           uint32_t vol_start_block, bool ro)
{
    read_fn    = reader;
    write_fn   = writer;
    vol_offset = vol_start_block;
    read_only  = ro;
    mounted    = false;

    crc32c_init();

    if (!sb_read()) return false;

    /* Replay journal if filesystem was not cleanly unmounted */
    if (sb.state != 0 && !read_only) {
        journal_replay();
    }

    if (!read_only) {
        sb.state = 1;          /* Mark dirty */
        sb.mount_count++;
        sb_write();
    }

    mounted = true;
    return true;
}

bool unmount(void)
{
    if (!mounted) return false;

    if (!read_only) {
        if (txn_active) journal_commit();
        sb.state = 0;          /* Mark clean */
        sb_write();
    }

    mounted    = false;
    read_fn    = nullptr;
    write_fn   = nullptr;
    return true;
}

/* ========================================================================
 * stat - get file information by path
 * ======================================================================== */

bool stat(const char *path, NeoStat *st)
{
    if (!mounted || !path || !st) return false;

    uint32_t ino = path_resolve(INO_ROOT, path);
    if (ino == INO_INVALID) return false;

    Inode inode;
    if (!inode_read(ino, &inode)) return false;

    st->ino        = ino;
    st->mode       = inode.mode;
    st->uid        = inode.uid;
    st->gid        = inode.gid;
    st->link_count = inode.link_count;
    st->size       = inode.size;
    st->block_count= inode.block_count;
    st->mtime      = inode.mtime;
    st->ctime      = inode.ctime;
    st->crtime     = inode.crtime;
    st->flags      = inode.flags;
    return true;
}

/* ========================================================================
 * read_file - read bytes from a file by path into a buffer
 * ======================================================================== */

bool read_file(const char *path, uint8_t *buf, uint64_t offset,
               uint32_t length, uint32_t *bytes_read)
{
    if (bytes_read) *bytes_read = 0;
    if (!mounted || !path || !buf || !length) return false;

    uint32_t ino = path_resolve(INO_ROOT, path);
    if (ino == INO_INVALID) return false;

    Inode inode;
    if (!inode_read(ino, &inode)) return false;
    if ((inode.mode & NFS_IFMT) != NFS_IFREG) return false;

    if (offset >= inode.size) return true; /* Past EOF, 0 bytes read */

    uint64_t available = inode.size - offset;
    if (length > available) length = (uint32_t)available;

    /* Inline data */
    if (inode.flags & NFS_FL_INLINE) {
        uint32_t copy = length;
        if (offset + copy > MAX_INLINE_BYTES)
            copy = (uint32_t)(MAX_INLINE_BYTES - offset);
        __builtin_memcpy(buf, inode.data.inline_data + offset, copy);
        if (bytes_read) *bytes_read = copy;
        return true;
    }

    /* Block-based read */
    uint32_t total_read = 0;

    while (total_read < length) {
        uint64_t file_offset = offset + total_read;
        uint32_t logical_blk = (uint32_t)(file_offset / BLOCK_SIZE);
        uint32_t blk_offset  = (uint32_t)(file_offset % BLOCK_SIZE);
        uint32_t can_read    = BLOCK_SIZE - blk_offset;
        if (can_read > length - total_read)
            can_read = length - total_read;

        uint32_t phys;
        if (!extent_lookup(&inode, logical_blk, &phys)) break;
        if (!blk_read(phys)) break;

        __builtin_memcpy(buf + total_read, io_buf + blk_offset, can_read);
        total_read += can_read;
    }

    if (bytes_read) *bytes_read = total_read;
    return (total_read > 0);
}

/* ========================================================================
 * write_file - write bytes to a file (creates extent and updates size)
 * ======================================================================== */

bool write_file(const char *path, const uint8_t *buf, uint64_t offset,
                uint32_t length, uint32_t *bytes_written)
{
    if (bytes_written) *bytes_written = 0;
    if (!mounted || read_only || !path || !buf || !length) return false;

    uint32_t ino = path_resolve(INO_ROOT, path);
    if (ino == INO_INVALID) return false;

    Inode inode;
    if (!inode_read(ino, &inode)) return false;
    if ((inode.mode & NFS_IFMT) != NFS_IFREG) return false;
    if (inode.flags & NFS_FL_IMMUTABLE) return false;
    if ((inode.flags & NFS_FL_APPEND) && offset < inode.size) return false;

    uint32_t total_written = 0;

    journal_begin_txn();

    while (total_written < length) {
        uint64_t file_offset  = offset + total_written;
        uint32_t logical_blk  = (uint32_t)(file_offset / BLOCK_SIZE);
        uint32_t blk_offset   = (uint32_t)(file_offset % BLOCK_SIZE);
        uint32_t can_write    = BLOCK_SIZE - blk_offset;
        if (can_write > length - total_written)
            can_write = length - total_written;

        uint32_t phys = 0;
        bool need_alloc = !extent_lookup(&inode, logical_blk, &phys);

        if (need_alloc) {
            phys = alloc_block();
            if (!phys) break;
            __builtin_memset(io_buf, 0, BLOCK_SIZE);
        } else {
            if (!blk_read(phys)) break;
        }

        journal_log(phys);

        __builtin_memcpy(io_buf + blk_offset, buf + total_written, can_write);
        if (!blk_write(phys)) {
            if (need_alloc) free_block(phys);
            break;
        }

        if (need_alloc) {
            if (!extent_append(&inode, logical_blk, phys, 1)) {
                free_block(phys);
                break;
            }
            inode.block_count += SECTORS_PER_BLOCK;
        }

        total_written += can_write;
    }

    /* Update file size if we wrote past EOF */
    uint64_t new_end = offset + total_written;
    if (new_end > inode.size) inode.size = new_end;

    inode_write(ino, &inode);
    journal_commit();

    if (bytes_written) *bytes_written = total_written;
    return (total_written > 0);
}

/* ========================================================================
 * create - create a new regular file
 * ======================================================================== */

bool create(const char *path, uint16_t mode)
{
    if (!mounted || read_only || !path) return false;

    /* Split path into parent directory and filename */
    const char *last_slash = nullptr;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *filename;
    char parent_path[512];
    uint32_t parent_ino;

    if (last_slash) {
        uint32_t plen = (uint32_t)(last_slash - path);
        if (plen == 0) {
            parent_ino = INO_ROOT;
        } else {
            if (plen >= sizeof(parent_path)) return false;
            __builtin_memcpy(parent_path, path, plen);
            parent_path[plen] = '\0';
            parent_ino = path_resolve(INO_ROOT, parent_path);
            if (parent_ino == INO_INVALID) return false;
        }
        filename = last_slash + 1;
    } else {
        parent_ino = INO_ROOT;
        filename   = path;
    }

    if (!*filename || str_len(filename) > MAX_NAME_LEN) return false;

    /* Check filename doesn't already exist */
    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;
    if (dir_lookup(&parent, filename) != INO_INVALID) return false; /* Exists */

    /* Allocate inode */
    uint32_t new_ino = alloc_inode();
    if (new_ino == INO_INVALID) return false;

    /* Initialise inode */
    Inode inode;
    __builtin_memset(&inode, 0, sizeof(inode));
    inode.mode       = (uint16_t)((mode & ~NFS_IFMT) | NFS_IFREG);
    inode.uid        = 0;
    inode.gid        = 0;
    inode.link_count = 1;
    inode.size       = 0;

    journal_begin_txn();

    if (!inode_write(new_ino, &inode)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    if (!dir_add_entry(&parent, parent_ino, new_ino, filename, DT_REG)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    sb.free_inodes--; /* Already decremented in alloc_inode, but update SB */
    journal_commit();
    return true;
}

/* ========================================================================
 * mkdir - create a directory
 * ======================================================================== */

bool mkdir(const char *path, uint16_t mode)
{
    if (!mounted || read_only || !path) return false;

    const char *last_slash = nullptr;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *dirname;
    char parent_path[512];
    uint32_t parent_ino;

    if (last_slash) {
        uint32_t plen = (uint32_t)(last_slash - path);
        if (plen == 0) {
            parent_ino = INO_ROOT;
        } else {
            if (plen >= sizeof(parent_path)) return false;
            __builtin_memcpy(parent_path, path, plen);
            parent_path[plen] = '\0';
            parent_ino = path_resolve(INO_ROOT, parent_path);
            if (parent_ino == INO_INVALID) return false;
        }
        dirname = last_slash + 1;
    } else {
        parent_ino = INO_ROOT;
        dirname    = path;
    }

    if (!*dirname || str_len(dirname) > MAX_NAME_LEN) return false;

    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;
    if (dir_lookup(&parent, dirname) != INO_INVALID) return false;

    uint32_t new_ino = alloc_inode();
    if (new_ino == INO_INVALID) return false;

    Inode inode;
    __builtin_memset(&inode, 0, sizeof(inode));
    inode.mode       = (uint16_t)((mode & ~NFS_IFMT) | NFS_IFDIR);
    inode.link_count = 2; /* Self + parent ref */
    inode.size       = 0;

    journal_begin_txn();

    if (!inode_write(new_ino, &inode)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    /* Add . and .. entries */
    dir_add_entry(&inode, new_ino, new_ino,   ".",  DT_DIR);
    dir_add_entry(&inode, new_ino, parent_ino, "..", DT_DIR);

    if (!dir_add_entry(&parent, parent_ino, new_ino, dirname, DT_DIR)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    /* Increment parent link_count for the new subdirectory */
    parent.link_count++;
    inode_write(parent_ino, &parent);

    journal_commit();
    return true;
}

/* ========================================================================
 * unlink - remove a file
 * ======================================================================== */

bool unlink(const char *path)
{
    if (!mounted || read_only || !path) return false;

    /* Resolve path and get parent */
    const char *last_slash = nullptr;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *filename;
    char parent_path[512];
    uint32_t parent_ino;

    if (last_slash) {
        uint32_t plen = (uint32_t)(last_slash - path);
        if (plen == 0) { parent_ino = INO_ROOT; }
        else {
            if (plen >= sizeof(parent_path)) return false;
            __builtin_memcpy(parent_path, path, plen);
            parent_path[plen] = '\0';
            parent_ino = path_resolve(INO_ROOT, parent_path);
            if (parent_ino == INO_INVALID) return false;
        }
        filename = last_slash + 1;
    } else {
        parent_ino = INO_ROOT;
        filename   = path;
    }

    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;

    uint32_t target_ino = dir_lookup(&parent, filename);
    if (target_ino == INO_INVALID) return false;

    Inode target;
    if (!inode_read(target_ino, &target)) return false;
    if ((target.mode & NFS_IFMT) == NFS_IFDIR) return false; /* Use rmdir */
    if (target.flags & NFS_FL_IMMUTABLE)  return false;

    journal_begin_txn();

    dir_remove_entry(&parent, parent_ino, filename);
    target.link_count--;

    if (target.link_count == 0) {
        /* Free all data blocks */
        uint32_t blocks = (uint32_t)((target.size + BLOCK_SIZE - 1) / BLOCK_SIZE);
        for (uint32_t lb = 0; lb < blocks; lb++) {
            uint32_t pb;
            if (extent_lookup(&target, lb, &pb)) free_block(pb);
        }
        /* Free overflow blocks */
        uint32_t ovf = target.overflow_block;
        while (ovf) {
            ExtentBlock eb;
            uint32_t next = 0;
            if (blk_read_to(ovf, &eb)) next = eb.overflow_next;
            free_block(ovf);
            ovf = next;
        }
        free_inode(target_ino);
    } else {
        inode_write(target_ino, &target);
    }

    journal_commit();
    return true;
}

/* ========================================================================
 * rmdir - remove an empty directory
 * ======================================================================== */

bool rmdir(const char *path)
{
    if (!mounted || read_only || !path) return false;

    const char *last_slash = nullptr;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *dirname;
    char parent_path[512];
    uint32_t parent_ino;

    if (last_slash) {
        uint32_t plen = (uint32_t)(last_slash - path);
        if (plen == 0) { parent_ino = INO_ROOT; }
        else {
            if (plen >= sizeof(parent_path)) return false;
            __builtin_memcpy(parent_path, path, plen);
            parent_path[plen] = '\0';
            parent_ino = path_resolve(INO_ROOT, parent_path);
            if (parent_ino == INO_INVALID) return false;
        }
        dirname = last_slash + 1;
    } else {
        parent_ino = INO_ROOT;
        dirname    = path;
    }

    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;

    uint32_t dir_ino = dir_lookup(&parent, dirname);
    if (dir_ino == INO_INVALID) return false;

    Inode dir;
    if (!inode_read(dir_ino, &dir)) return false;
    if ((dir.mode & NFS_IFMT) != NFS_IFDIR) return false;
    if (!dir_is_empty(&dir)) return false; /* ENOTEMPTY */

    journal_begin_txn();

    /* Free directory data blocks */
    uint32_t blocks = (uint32_t)((dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    for (uint32_t lb = 0; lb < blocks; lb++) {
        uint32_t pb;
        if (extent_lookup(&dir, lb, &pb)) free_block(pb);
    }
    free_inode(dir_ino);

    dir_remove_entry(&parent, parent_ino, dirname);
    parent.link_count--;
    inode_write(parent_ino, &parent);

    journal_commit();
    return true;
}

/* ========================================================================
 * rename - move/rename a file or directory
 * ======================================================================== */

bool rename(const char *old_path, const char *new_path)
{
    if (!mounted || read_only || !old_path || !new_path) return false;

    /* Resolve source */
    uint32_t src_ino = path_resolve(INO_ROOT, old_path);
    if (src_ino == INO_INVALID) return false;

    /* Extract source parent + name */
    const char *old_slash = nullptr;
    for (const char *p = old_path; *p; p++)
        if (*p == '/') old_slash = p;
    const char *old_name = old_slash ? old_slash + 1 : old_path;

    char old_parent_buf[512];
    uint32_t old_parent_ino = INO_ROOT;
    if (old_slash && old_slash != old_path) {
        uint32_t l = (uint32_t)(old_slash - old_path);
        if (l < sizeof(old_parent_buf)) {
            __builtin_memcpy(old_parent_buf, old_path, l);
            old_parent_buf[l] = '\0';
            old_parent_ino = path_resolve(INO_ROOT, old_parent_buf);
            if (old_parent_ino == INO_INVALID) return false;
        }
    }

    /* Extract dest parent + name */
    const char *new_slash = nullptr;
    for (const char *p = new_path; *p; p++)
        if (*p == '/') new_slash = p;
    const char *new_name = new_slash ? new_slash + 1 : new_path;

    char new_parent_buf[512];
    uint32_t new_parent_ino = INO_ROOT;
    if (new_slash && new_slash != new_path) {
        uint32_t l = (uint32_t)(new_slash - new_path);
        if (l < sizeof(new_parent_buf)) {
            __builtin_memcpy(new_parent_buf, new_path, l);
            new_parent_buf[l] = '\0';
            new_parent_ino = path_resolve(INO_ROOT, new_parent_buf);
            if (new_parent_ino == INO_INVALID) return false;
        }
    }

    Inode src_inode;
    if (!inode_read(src_ino, &src_inode)) return false;
    if (src_inode.flags & NFS_FL_IMMUTABLE) return false;

    Inode new_parent;
    if (!inode_read(new_parent_ino, &new_parent)) return false;

    uint8_t ft = ((src_inode.mode & NFS_IFMT) == NFS_IFDIR) ? DT_DIR : DT_REG;

    journal_begin_txn();

    /* If destination exists, remove it first */
    uint32_t existing = dir_lookup(&new_parent, new_name);
    if (existing != INO_INVALID) {
        Inode ex_inode;
        if (inode_read(existing, &ex_inode)) {
            if ((ex_inode.mode & NFS_IFMT) == NFS_IFDIR) {
                if (!dir_is_empty(&ex_inode)) {
                    journal_commit();
                    return false; /* Dest dir not empty */
                }
                free_inode(existing);
                new_parent.link_count--;
            } else {
                ex_inode.link_count--;
                if (ex_inode.link_count == 0) free_inode(existing);
                else inode_write(existing, &ex_inode);
            }
        }
        dir_remove_entry(&new_parent, new_parent_ino, new_name);
    }

    /* Remove from old location, add to new location */
    Inode old_parent;
    if (!inode_read(old_parent_ino, &old_parent)) {
        journal_commit();
        return false;
    }

    dir_remove_entry(&old_parent, old_parent_ino, old_name);
    dir_add_entry(&new_parent, new_parent_ino, src_ino, new_name, ft);

    /* Update .. if moving a directory between parents */
    if (ft == DT_DIR && old_parent_ino != new_parent_ino) {
        dir_remove_entry(&src_inode, src_ino, "..");
        dir_add_entry(&src_inode, src_ino, new_parent_ino, "..", DT_DIR);
        old_parent.link_count--;
        new_parent.link_count++;
        inode_write(old_parent_ino, &old_parent);
        inode_write(new_parent_ino, &new_parent);
    }

    journal_commit();
    return true;
}

/* ========================================================================
 * readdir - enumerate directory entries
 * ======================================================================== */

bool readdir(const char *path, NeosDirEntry *entries, int max_entries,
             int *count)
{
    if (count) *count = 0;
    if (!mounted || !path || !entries || max_entries <= 0) return false;

    uint32_t dir_ino = path_resolve(INO_ROOT, path);
    if (dir_ino == INO_INVALID) return false;

    Inode dir;
    if (!inode_read(dir_ino, &dir)) return false;
    if ((dir.mode & NFS_IFMT) != NFS_IFDIR) return false;

    int n = 0;
    uint32_t blocks = (uint32_t)((dir.size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < blocks && n < max_entries; lb++) {
        uint32_t pb;
        if (!extent_lookup(&dir, lb, &pb)) continue;
        if (!blk_read(pb)) continue;

        const uint8_t *ptr = io_buf;
        const uint8_t *end = io_buf + BLOCK_SIZE;

        while (ptr + DIRENT_MIN_REC <= end && n < max_entries) {
            const DirEntry *de = (const DirEntry *)ptr;
            if (de->rec_len == 0) break;

            if (de->inode != INO_INVALID) {
                entries[n].ino  = de->inode;
                entries[n].type = de->file_type;
                uint8_t nl      = de->name_len;
                if (nl > MAX_NAME_LEN) nl = (uint8_t)MAX_NAME_LEN;
                __builtin_memcpy(entries[n].name, de->name, nl);
                entries[n].name[nl] = '\0';
                n++;
            }

            ptr += de->rec_len;
        }
    }

    if (count) *count = n;
    return true;
}

/* ========================================================================
 * symlink - create a symbolic link
 * ======================================================================== */

bool symlink(const char *target, const char *link_path)
{
    if (!mounted || read_only || !target || !link_path) return false;

    const char *last_slash = nullptr;
    for (const char *p = link_path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *linkname;
    char parent_buf[512];
    uint32_t parent_ino = INO_ROOT;

    if (last_slash && last_slash != link_path) {
        uint32_t l = (uint32_t)(last_slash - link_path);
        if (l >= sizeof(parent_buf)) return false;
        __builtin_memcpy(parent_buf, link_path, l);
        parent_buf[l] = '\0';
        parent_ino = path_resolve(INO_ROOT, parent_buf);
        if (parent_ino == INO_INVALID) return false;
    }
    linkname = last_slash ? last_slash + 1 : link_path;

    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;
    if (dir_lookup(&parent, linkname) != INO_INVALID) return false;

    uint32_t new_ino = alloc_inode();
    if (new_ino == INO_INVALID) return false;

    Inode inode;
    __builtin_memset(&inode, 0, sizeof(inode));
    inode.mode       = (uint16_t)(NFS_IFLNK | 0777);
    inode.link_count = 1;

    uint8_t tlen = str_len(target);
    if (tlen <= MAX_INLINE_BYTES) {
        inode.flags = NFS_FL_INLINE;
        inode.size  = tlen;
        __builtin_memcpy(inode.data.inline_data, target, tlen);
    } else {
        return false; /* Long symlinks not supported */
    }

    journal_begin_txn();

    if (!inode_write(new_ino, &inode)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    if (!dir_add_entry(&parent, parent_ino, new_ino, linkname, DT_LNK)) {
        free_inode(new_ino);
        journal_commit();
        return false;
    }

    journal_commit();
    return true;
}

/* ========================================================================
 * readlink - read a symlink target
 * ======================================================================== */

bool readlink(const char *path, char *buf, uint32_t buf_size)
{
    if (!mounted || !path || !buf || !buf_size) return false;

    /* Don't follow the final symlink for readlink */
    uint32_t ino = INO_ROOT;
    /* Navigate to parent then look up directly (no symlink follow on last) */
    const char *last_slash = nullptr;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *name;
    char parent_buf[512];
    uint32_t parent_ino = INO_ROOT;

    if (last_slash && last_slash != path) {
        uint32_t l = (uint32_t)(last_slash - path);
        if (l >= sizeof(parent_buf)) return false;
        __builtin_memcpy(parent_buf, path, l);
        parent_buf[l] = '\0';
        parent_ino = path_resolve(INO_ROOT, parent_buf);
        if (parent_ino == INO_INVALID) return false;
    }
    name = last_slash ? last_slash + 1 : path;

    Inode parent;
    if (!inode_read(parent_ino, &parent)) return false;
    ino = dir_lookup(&parent, name);
    if (ino == INO_INVALID) return false;

    Inode inode;
    if (!inode_read(ino, &inode)) return false;
    if ((inode.mode & NFS_IFMT) != NFS_IFLNK) return false;

    if (inode.flags & NFS_FL_INLINE) {
        uint32_t len = (uint32_t)inode.size;
        if (len >= buf_size) len = buf_size - 1;
        __builtin_memcpy(buf, inode.data.inline_data, len);
        buf[len] = '\0';
        return true;
    }
    return false;
}

/* ========================================================================
 * format - initialise a new NeoFS volume
 * ======================================================================== */

bool format(NeoWriteFunc writer, uint32_t start_block,
            uint32_t total_4kb_blocks, const char *label)
{
    if (!writer || total_4kb_blocks < 64) return false;

    write_fn   = writer;
    read_fn    = nullptr;
    vol_offset = start_block;
    read_only  = false;

    crc32c_init();

    /* Calculate geometry */
    uint32_t n_inodes      = total_4kb_blocks / 4; /* ~1 inode per 4 data blocks */
    if (n_inodes < 64) n_inodes = 64;

    uint32_t ino_bmp_blks  = (n_inodes + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    uint32_t blk_bmp_blks  = (total_4kb_blocks + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    uint32_t ino_tbl_blks  = (n_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
    uint32_t journal_blks  = 128; /* 128 × 4KB = 512KB journal */

    uint32_t ino_bmp_start = 2;
    uint32_t blk_bmp_start = ino_bmp_start + ino_bmp_blks;
    uint32_t ino_tbl_start = blk_bmp_start + blk_bmp_blks;
    uint32_t jnl_start     = ino_tbl_start + ino_tbl_blks;
    uint32_t data_start    = jnl_start     + journal_blks;

    if (data_start >= total_4kb_blocks) return false; /* Volume too small */

    uint32_t data_blocks   = total_4kb_blocks - data_start;

    /* Zero all metadata blocks */
    uint8_t zero[BLOCK_SIZE];
    __builtin_memset(zero, 0, BLOCK_SIZE);

    for (uint32_t b = 0; b < data_start; b++)
        writer(start_block + b, zero);

    /* Build superblock */
    __builtin_memset(&sb, 0, sizeof(sb));
    sb.magic                = NEOFS_MAGIC;
    sb.version              = NEOFS_VERSION;
    sb.block_size           = BLOCK_SIZE;
    sb.total_blocks         = total_4kb_blocks;
    sb.free_blocks          = data_blocks;
    sb.total_inodes         = n_inodes;
    sb.free_inodes          = n_inodes - INO_FIRST_USER;
    sb.inode_bitmap_start   = ino_bmp_start;
    sb.inode_bitmap_blocks  = ino_bmp_blks;
    sb.block_bitmap_start   = blk_bmp_start;
    sb.block_bitmap_blocks  = blk_bmp_blks;
    sb.inode_table_start    = ino_tbl_start;
    sb.inode_table_blocks   = ino_tbl_blks;
    sb.data_start           = data_start;
    sb.journal_blocks       = journal_blks;
    sb.journal_head         = 0;
    sb.journal_tail         = 0;
    sb.journal_txn_id       = 1;
    sb.state                = 0;
    sb.mount_count          = 0;
    sb.max_mount_count      = 32;
    sb.creator_os           = 0; /* NeoBench */
    sb.uuid_hi              = (uint32_t)0xDEADBE00 ^ total_4kb_blocks;
    sb.uuid_lo              = (uint32_t)0xEFCAFE00 ^ data_start;

    /* Copy volume label */
    if (label) {
        uint8_t ll = str_len(label);
        if (ll > 31) ll = 31;
        __builtin_memcpy(sb.volume_name, label, ll);
        sb.volume_name[ll] = '\0';
    }

    /* Reserve inodes 1 through INO_FIRST_USER-1 in inode bitmap */
    uint8_t ino_bmp[BLOCK_SIZE];
    __builtin_memset(ino_bmp, 0, BLOCK_SIZE);
    for (uint32_t i = 0; i < INO_FIRST_USER; i++) {
        ino_bmp[i / 8] |= (uint8_t)(1u << (i % 8));
    }
    writer(start_block + ino_bmp_start, ino_bmp);

    /* Reserve all metadata blocks in block bitmap */
    uint8_t blk_bmp[BLOCK_SIZE];
    __builtin_memset(blk_bmp, 0, BLOCK_SIZE);
    /* Blocks 0 through data_start-1 are metadata */
    for (uint32_t b = 0; b < data_start; b++) {
        blk_bmp[b / 8] |= (uint8_t)(1u << (b % 8));
    }
    writer(start_block + blk_bmp_start, blk_bmp);

    /* Write superblock to both locations */
    sb.crc32 = 0;
    sb.crc32 = crc32c(&sb, sizeof(Superblock));
    uint8_t sb_block[BLOCK_SIZE];
    __builtin_memset(sb_block, 0, BLOCK_SIZE);
    __builtin_memcpy(sb_block, &sb, sizeof(Superblock));
    writer(start_block + SB_PRIMARY, sb_block);
    writer(start_block + SB_MIRROR,  sb_block);

    /* Now mount read/write to use the inode/directory APIs */
    read_fn = nullptr; /* We need a reader - format caller must supply one */
    /* Write root inode directly using internal helper */
    /* Since we don't have read_fn yet, we write the inode table block directly */

    /* Build root directory inode */
    Inode root;
    __builtin_memset(&root, 0, sizeof(root));
    root.mode       = (uint16_t)(NFS_IFDIR | 0755);
    root.link_count = 2;    /* . and itself from parent (no parent for root) */
    root.size       = 0;
    root.crc32      = 0;
    root.crc32      = crc32c(&root, sizeof(root));

    /* Write root inode (inode 1, index 0) into inode table */
    uint8_t ino_block[BLOCK_SIZE];
    __builtin_memset(ino_block, 0, BLOCK_SIZE);
    __builtin_memcpy(ino_block, &root, sizeof(root));
    writer(start_block + ino_tbl_start, ino_block);

    return true;
}

/* ========================================================================
 * Info accessors
 * ======================================================================== */

bool is_mounted(void)                { return mounted; }
bool is_read_only(void)              { return read_only; }
uint32_t get_free_blocks(void)       { return mounted ? sb.free_blocks : 0; }
uint32_t get_free_inodes(void)       { return mounted ? sb.free_inodes : 0; }
uint64_t get_free_bytes(void)        { return (uint64_t)get_free_blocks() * BLOCK_SIZE; }
uint32_t get_total_blocks(void)      { return mounted ? sb.total_blocks : 0; }
const char *get_volume_label(void)   { return mounted ? sb.volume_name : ""; }
uint32_t get_block_size(void)        { return BLOCK_SIZE; }

} /* namespace neofs */
} /* namespace neo */
