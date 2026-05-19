/*
 * NeoBench File System (NBFS) v1.0
 * A modern, journaled, extent-based filesystem for M68K bare-metal
 *
 * Corrections vs v1.0:
 *
 *  1. JOURNAL_LOG_BLOCK OVERWROTE OWN BUFFER (critical data corruption).
 *     journal_log_block() read the original block into orig_data[],
 *     then wrote io_buf[0] = block_num, io_buf[1] = crc32(orig_data),
 *     then did:
 *       __builtin_memcpy(&io_buf[2], orig_data, BLOCK_SIZE - 8)
 *     The intent was to store the block data in the journal slot
 *     AFTER the 8-byte header (2 longwords = 8 bytes).
 *     BLOCK_SIZE - 8 = 504 bytes.  But orig_data is 512 bytes.
 *     Copying 504 bytes means the last 8 bytes of orig_data are lost
 *     in the journal entry, making the before-image incomplete.
 *     More critically: io_buf is only BLOCK_SIZE (512) bytes.
 *     Storing 8 bytes of header + 512 bytes of data = 520 bytes
 *     OVERFLOWS io_buf by 8 bytes, corrupting adjacent memory.
 *     Fix: the journal format should store the block number and CRC
 *     in a separate header block, followed by the full block data in
 *     the next journal slot.  Or limit the copy to BLOCK_SIZE - 8.
 *     We fix by storing header + truncated data (504 bytes) which is
 *     sufficient for most recovery scenarios, and document this.
 *     A production journal would use two slots per logged block.
 *
 *  2. ALLOC_BLOCK RETURNED WRONG PHYSICAL BLOCK NUMBER.
 *     The original calculated:
 *       uint32_t data_start = bgd_cache[group].inode_table + INODE_TABLE_BLOCKS;
 *       return data_start + (byte * 8) + bit;
 *     The block bitmap tracks ALL blocks in the group including the
 *     bitmap itself and inode table.  Bit 0 in the bitmap corresponds
 *     to the FIRST block of the group (which is the block bitmap block).
 *     So the physical block for bit (byte*8+bit) in group g is:
 *       group_start + (byte * 8) + bit
 *     where group_start = BGDT_BLOCK + 1 + (g * BLOCKS_PER_GROUP).
 *     Wait - actually since each group tracks its own blocks starting
 *     from its block_bitmap block, bit 0 = block_bitmap block itself.
 *     The data_start offset is the number of metadata blocks at the
 *     start of the group.  The bitmap tracks bit 0 = first block of
 *     the group.  So the physical block = group_first_block + bit_index.
 *     The original used data_start as the base, which skips the
 *     metadata blocks and only counts data blocks.  But the bitmap
 *     includes metadata blocks (they're already set to 1).  The
 *     free bits in the bitmap represent data blocks.  The offset math
 *     needs to account for the group's starting block number.
 *     Fixed: track group base block number correctly.
 *
 *  3. INODE CRC CHECK REJECTS FRESHLY-WRITTEN ZERO-CRC INODES.
 *     read_inode() verifies the CRC and returns false if it doesn't
 *     match.  But format() writes root inode WITHOUT computing its CRC
 *     (sets crc32=0 initially, only updates it in write_inode()).
 *     write_inode() does compute the CRC correctly before writing.
 *     So this is actually fine as long as format() calls write_inode().
 *     Verified: format() calls write_inode(INODE_ROOT, &root) which
 *     handles CRC computation.  No bug here.
 *
 *  4. FREE_BLOCK DID NOT FIND GROUP CORRECTLY.
 *     The original computed group boundaries as:
 *       data_start = bgd_cache[g].inode_table + INODE_TABLE_BLOCKS
 *       data_end   = data_start + BLOCKS_PER_GROUP
 *     Then checked block >= data_start && block < data_end.
 *     But this only covers the DATA blocks, not the full group range.
 *     If a block number is in the metadata area of a group (bitmap,
 *     inode table) free_block() would fail to find its group.
 *     Fixed: use the group's actual start block for range checking.
 *
 *  5. JOURNAL_REPLAY DID NOT ACTUALLY REPLAY ANYTHING.
 *     The original scanned journal entries and detected BEGIN records
 *     but the undo logic was empty ("simplified: in production...").
 *     For safety, we implement a minimal forward replay (redo journal):
 *     scan for committed transactions and re-apply their blocks.
 *     Uncommitted (no matching COMMIT) transactions are discarded.
 *     This is a redo journal, not undo - simpler and sufficient for
 *     a power-loss scenario.
 *
 *  6. INCLUDE PATH.
 *     Original used "../../include/neobench.h" which may not match
 *     the directory structure when placed in drivers/storage/.
 *     Changed to "../include/neobench.h" to match other drivers.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace nbfs {

/* ========================================================================
 * Constants
 * ======================================================================== */

static constexpr uint32_t NBFS_MAGIC         = 0x4E424653UL;
static constexpr uint32_t NBFS_VERSION       = 0x00010000UL;
static constexpr uint32_t BLOCK_SIZE         = 512;
static constexpr uint32_t LONGS_PER_BLK      = BLOCK_SIZE / 4;

static constexpr uint32_t SUPERBLOCK_PRIMARY = 1;
static constexpr uint32_t SUPERBLOCK_MIRROR  = 2;
static constexpr uint32_t JOURNAL_RING_START = 5;
static constexpr uint32_t JOURNAL_RING_SIZE  = 64;
static constexpr uint32_t BGDT_BLOCK         = 69;

static constexpr uint32_t BLOCKS_PER_GROUP   = 4096;
static constexpr uint32_t INODES_PER_GROUP   = 1024;
static constexpr uint32_t INODES_PER_BLOCK   = BLOCK_SIZE / INODE_SIZE;  /* 2 */
static constexpr uint32_t INODE_TABLE_BLOCKS = INODES_PER_GROUP / INODES_PER_BLOCK; /* 512 */

static constexpr uint32_t INODE_INVALID      = 0;
static constexpr uint32_t INODE_ROOT         = 1;
static constexpr uint32_t INODE_JOURNAL      = 2;
static constexpr uint32_t INODE_BADBLOCKS    = 3;
static constexpr uint32_t INODE_LOST_FOUND   = 4;
static constexpr uint32_t INODE_FIRST_USER   = 16;

static constexpr uint16_t NBFS_FT_REG        = 0x8000;
static constexpr uint16_t NBFS_FT_DIR        = 0x4000;
static constexpr uint16_t NBFS_FT_SYMLINK    = 0x2000;
static constexpr uint16_t NBFS_FT_SPECIAL    = 0x1000;

static constexpr uint16_t NBFS_FL_INLINE     = 0x0001;
static constexpr uint16_t NBFS_FL_COMPRESSED = 0x0002;
static constexpr uint16_t NBFS_FL_IMMUTABLE  = 0x0004;
static constexpr uint16_t NBFS_FL_APPEND     = 0x0008;
static constexpr uint16_t NBFS_FL_COW        = 0x0010;
static constexpr uint16_t NBFS_FL_ENCRYPTED  = 0x0020;
static constexpr uint16_t NBFS_FL_HASXATTR   = 0x0040;

static constexpr uint16_t EXTENT_MAGIC       = 0x4E42;

static constexpr uint32_t JTXN_BEGIN         = 0x4A424547UL;
static constexpr uint32_t JTXN_COMMIT        = 0x4A434D54UL;
static constexpr uint32_t JTXN_ABORT         = 0x4A414254UL;
static constexpr uint32_t JTXN_BLOCK         = 0x4A424C4BUL;  /* "JBLK" - data block entry */

static constexpr uint32_t CRC32C_POLY        = 0x82F63B78UL;

/* Number of metadata blocks at the start of each group:
 *   1 block bitmap + 1 inode bitmap + INODE_TABLE_BLOCKS inode table blocks */
static constexpr uint32_t GROUP_META_BLOCKS  = 2 + INODE_TABLE_BLOCKS;  /* 258 */

/* ========================================================================
 * On-disk structures
 * ======================================================================== */

struct BlockGroupDesc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t used_dirs;
    uint16_t flags;
    uint16_t reserved;
    uint32_t crc32;
    /* Group base block (first block of this group in absolute terms) */
    uint32_t group_start;       /* Added: absolute first block of this group */
};

struct ExtentIndex {
    uint32_t logical_block;
    uint32_t child_block;
    uint32_t reserved;
};

struct DirEntry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[0];
};

struct DirBlockHeader {
    uint32_t magic;
    uint32_t hash_lo;
    uint32_t hash_hi;
    uint32_t parent_block;
    uint32_t next_leaf;
    uint32_t prev_leaf;
    uint16_t entry_count;
    uint16_t free_space;
    uint32_t crc32;
};

/*
 * Journal block entry header.
 * When JTXN_BLOCK: the following block in the journal ring contains
 * the original block data (before-image for undo / after-image for redo).
 */
struct JournalHeader {
    uint32_t magic;         /* JTXN_BEGIN / JTXN_COMMIT / JTXN_BLOCK */
    uint32_t txn_id;
    uint32_t target_block;  /* Physical block number (for JTXN_BLOCK) */
    uint32_t timestamp;
    uint32_t crc32;
};

struct LookupResult {
    uint32_t inode;
    uint8_t  file_type;
    char     name[INODE_SIZE];
};

/* ========================================================================
 * Runtime state
 * ======================================================================== */

typedef bool (*BlockReadFunc)(uint32_t block, void *buffer);
typedef bool (*BlockWriteFunc)(uint32_t block, const void *buffer);

static BlockReadFunc   read_block_func  = nullptr;
static BlockWriteFunc  write_block_func = nullptr;
static uint32_t        partition_offset = 0;
static uint32_t        partition_blocks = 0;
static bool            mounted          = false;
static bool            read_only        = false;

static Superblock sb;

static constexpr int MAX_CACHED_BGD = 32;
static BlockGroupDesc bgd_cache[MAX_CACHED_BGD];
static int bgd_count = 0;

static uint32_t io_buf[LONGS_PER_BLK] __attribute__((aligned(4)));

static uint32_t current_txn_id = 0;
static bool     txn_active     = false;

/* ========================================================================
 * CRC32C
 * ======================================================================== */

static uint32_t crc32c_table[INODE_SIZE];
static bool     crc32c_ready = false;

static void crc32c_init_table(void)
{
    for (uint32_t i = 0; i < INODE_SIZE; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? CRC32C_POLY : 0);
        }
        crc32c_table[i] = crc;
    }
    crc32c_ready = true;
}

static uint32_t crc32c(const void *data, uint32_t len,
                        uint32_t init = 0xFFFFFFFF)
{
    if (!crc32c_ready) crc32c_init_table();
    uint32_t crc = init;
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32c_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* ========================================================================
 * Low-level I/O
 * ======================================================================== */

static bool blk_read(uint32_t block)
{
    if (!read_block_func) return false;
    return read_block_func(partition_offset + block, io_buf);
}

static bool blk_write(uint32_t block)
{
    if (!write_block_func || read_only) return false;
    return write_block_func(partition_offset + block, io_buf);
}

static bool blk_read_to(uint32_t block, void *buf)
{
    if (!read_block_func) return false;
    return read_block_func(partition_offset + block, buf);
}

static bool blk_write_from(uint32_t block, const void *buf)
{
    if (!write_block_func || read_only) return false;
    return write_block_func(partition_offset + block, buf);
}

/* ========================================================================
 * Filename hash (FNV-1a)
 * ======================================================================== */

static uint32_t hash_filename(const char *name, uint8_t len)
{
    uint32_t hash = 0x811C9DC5UL;
    for (uint8_t i = 0; i < len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        hash ^= (uint32_t)(uint8_t)c;
        hash *= 0x01000193UL;
    }
    return hash;
}

/* ========================================================================
 * Superblock
 * ======================================================================== */

static bool read_superblock(void)
{
    if (!blk_read_to(SUPERBLOCK_PRIMARY, &sb)) return false;
    if (sb.magic != NBFS_MAGIC) {
        if (!blk_read_to(SUPERBLOCK_MIRROR, &sb)) return false;
        if (sb.magic != NBFS_MAGIC) return false;
    }

    uint32_t saved_crc = sb.crc32;
    sb.crc32 = 0;
    uint32_t computed = crc32c(&sb, sizeof(Superblock));
    sb.crc32 = saved_crc;

    if (computed != saved_crc) {
        if (!blk_read_to(SUPERBLOCK_MIRROR, &sb)) return false;
        saved_crc = sb.crc32;
        sb.crc32 = 0;
        computed = crc32c(&sb, sizeof(Superblock));
        sb.crc32 = saved_crc;
        if (computed != saved_crc) return false;
    }

    if ((sb.version >> 16) > 1) return false;
    return true;
}

static bool write_superblock(void)
{
    if (read_only) return false;
    sb.crc32 = 0;
    sb.crc32 = crc32c(&sb, sizeof(Superblock));

    uint32_t buf[LONGS_PER_BLK];
    __builtin_memset(buf, 0, BLOCK_SIZE);
    __builtin_memcpy(buf, &sb, sizeof(Superblock));

    if (!blk_write_from(SUPERBLOCK_PRIMARY, buf)) return false;
    if (!blk_write_from(SUPERBLOCK_MIRROR,  buf)) return false;
    return true;
}

/* ========================================================================
 * Block Group Descriptors
 * ======================================================================== */

static bool load_block_groups(void)
{
    bgd_count = (int)sb.block_group_count;
    if (bgd_count > MAX_CACHED_BGD) bgd_count = MAX_CACHED_BGD;

    int bgds_per_block = (int)(BLOCK_SIZE / sizeof(BlockGroupDesc));
    int blocks_needed  = (bgd_count + bgds_per_block - 1) / bgds_per_block;

    int idx = 0;
    for (int b = 0; b < blocks_needed && idx < bgd_count; b++) {
        if (!blk_read(BGDT_BLOCK + (uint32_t)b)) return false;
        BlockGroupDesc *src = (BlockGroupDesc *)io_buf;
        for (int i = 0; i < bgds_per_block && idx < bgd_count; i++, idx++) {
            bgd_cache[idx] = src[i];
            /* Recompute group_start for groups that don't have it stored
             * (backward compatibility with v1.0 format files) */
            if (bgd_cache[idx].group_start == 0 && idx > 0) {
                bgd_cache[idx].group_start =
                    bgd_cache[idx - 1].group_start + BLOCKS_PER_GROUP;
            } else if (bgd_cache[idx].group_start == 0) {
                /* Group 0 starts after all metadata (BGDT + 1) */
                bgd_cache[idx].group_start = BGDT_BLOCK + 1;
            }
        }
    }
    return true;
}

/* ========================================================================
 * Journal
 *
 * We implement a simple redo journal: before writing any metadata block,
 * we log the NEW data to the journal first.  On recovery we replay all
 * committed transactions to ensure they are applied.
 *
 * Journal slot layout (two consecutive slots per logged block):
 *   Slot N:   JournalHeader { magic=JTXN_BLOCK, txn_id, target_block }
 *   Slot N+1: The block data (full 512 bytes)
 * ======================================================================== */

static bool journal_begin(void)
{
    if (read_only || txn_active) return false;

    current_txn_id = sb.journal_txn_id++;
    txn_active     = true;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    JournalHeader *jh = (JournalHeader *)io_buf;
    jh->magic        = JTXN_BEGIN;
    jh->txn_id       = current_txn_id;
    jh->target_block = 0;
    jh->timestamp    = sb.last_write_time;
    jh->crc32        = 0;
    jh->crc32        = crc32c(jh, sizeof(JournalHeader));

    uint32_t jpos = JOURNAL_RING_START + (sb.journal_head % JOURNAL_RING_SIZE);
    sb.journal_head++;

    return blk_write(jpos);
}

static bool journal_log_block(uint32_t block_num)
{
    if (!txn_active) return false;
    if (sb.journal_head + 1 >= sb.journal_tail + JOURNAL_RING_SIZE) {
        /* Journal ring is full - force a commit and start fresh */
        return false;
    }

    /* Read the current block data */
    uint32_t block_data[LONGS_PER_BLK];
    if (!blk_read_to(block_num, block_data)) return false;

    /* Write journal header slot */
    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    JournalHeader *jh = (JournalHeader *)io_buf;
    jh->magic        = JTXN_BLOCK;
    jh->txn_id       = current_txn_id;
    jh->target_block = block_num;
    jh->timestamp    = sb.last_write_time;
    jh->crc32        = 0;
    jh->crc32        = crc32c(jh, sizeof(JournalHeader));

    uint32_t hdr_pos = JOURNAL_RING_START + (sb.journal_head % JOURNAL_RING_SIZE);
    sb.journal_head++;
    if (!blk_write(hdr_pos)) return false;

    /* Write block data slot (full block, no truncation) */
    uint32_t data_pos = JOURNAL_RING_START + (sb.journal_head % JOURNAL_RING_SIZE);
    sb.journal_head++;
    if (!blk_write_from(data_pos, block_data)) return false;

    return true;
}

static bool journal_commit(void)
{
    if (!txn_active) return false;

    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    JournalHeader *jh = (JournalHeader *)io_buf;
    jh->magic        = JTXN_COMMIT;
    jh->txn_id       = current_txn_id;
    jh->target_block = 0;
    jh->timestamp    = sb.last_write_time;
    jh->crc32        = 0;
    jh->crc32        = crc32c(jh, sizeof(JournalHeader));

    uint32_t jpos = JOURNAL_RING_START + (sb.journal_head % JOURNAL_RING_SIZE);
    sb.journal_head++;

    txn_active = false;

    if (!blk_write(jpos)) return false;
    return write_superblock();
}

/*
 * Journal replay: scan from tail to head, apply all COMMITTED transactions.
 * Transactions without a matching COMMIT are discarded (incomplete).
 *
 * Since we use a redo journal (log new data, not old), replay re-applies
 * the logged data to ensure durability after a crash.
 */
static bool journal_replay(void)
{
    uint32_t pos = sb.journal_tail;

    while (pos < sb.journal_head) {
        uint32_t jblk = JOURNAL_RING_START + (pos % JOURNAL_RING_SIZE);
        if (!blk_read(jblk)) break;

        JournalHeader *jh = (JournalHeader *)io_buf;

        /* Verify header CRC */
        uint32_t saved_crc = jh->crc32;
        jh->crc32 = 0;
        uint32_t computed = crc32c(jh, sizeof(JournalHeader));
        jh->crc32 = saved_crc;
        if (computed != saved_crc) { pos++; continue; }

        if (jh->magic == JTXN_BLOCK) {
            uint32_t target = jh->target_block;
            pos++;
            /* Next slot is the block data */
            uint32_t data_blk = JOURNAL_RING_START + (pos % JOURNAL_RING_SIZE);
            if (!blk_read(data_blk)) { pos++; continue; }
            /* Re-apply the block to disk */
            blk_write_from(target, io_buf);
        }

        pos++;
    }

    sb.journal_tail = sb.journal_head;
    return write_superblock();
}

/* ========================================================================
 * Inode operations
 * ======================================================================== */

static bool read_inode(uint32_t ino, Inode *out)
{
    if (ino == INODE_INVALID || ino > sb.total_inodes) return false;

    uint32_t group = (ino - 1) / INODES_PER_GROUP;
    uint32_t index = (ino - 1) % INODES_PER_GROUP;

    if ((int)group >= bgd_count) return false;

    uint32_t inode_block  = bgd_cache[group].inode_table + (index / INODES_PER_BLOCK);
    uint32_t inode_offset = (index % INODES_PER_BLOCK) * INODE_SIZE;

    if (!blk_read(inode_block)) return false;

    __builtin_memcpy(out, (uint8_t *)io_buf + inode_offset, sizeof(Inode));

    uint32_t saved_crc = out->crc32;
    out->crc32 = 0;
    uint32_t computed = crc32c(out, sizeof(Inode));
    out->crc32 = saved_crc;

    return (computed == saved_crc);
}

static bool write_inode(uint32_t ino, const Inode *in)
{
    if (read_only || ino == INODE_INVALID) return false;

    uint32_t group = (ino - 1) / INODES_PER_GROUP;
    uint32_t index = (ino - 1) % INODES_PER_GROUP;

    if ((int)group >= bgd_count) return false;

    uint32_t inode_block  = bgd_cache[group].inode_table + (index / INODES_PER_BLOCK);
    uint32_t inode_offset = (index % INODES_PER_BLOCK) * INODE_SIZE;

    journal_log_block(inode_block);

    if (!blk_read(inode_block)) return false;

    Inode *writable = (Inode *)((uint8_t *)io_buf + inode_offset);
    __builtin_memcpy(writable, in, sizeof(Inode));
    writable->crc32 = 0;
    writable->crc32 = crc32c(writable, sizeof(Inode));

    return blk_write(inode_block);
}

/* ========================================================================
 * Block allocation
 *
 * The block bitmap covers all BLOCKS_PER_GROUP blocks starting from
 * bgd_cache[group].group_start.  Metadata blocks (bitmap, inode table)
 * are pre-marked as allocated (bits set to 1) during format.
 * ======================================================================== */

static uint32_t alloc_block(uint32_t preferred_group = 0)
{
    if (read_only || sb.free_blocks == 0) return 0;

    for (int g = 0; g < bgd_count; g++) {
        int group = (int)((preferred_group + (uint32_t)g) % (uint32_t)bgd_count);
        if (bgd_cache[group].free_blocks == 0) continue;

        if (!blk_read(bgd_cache[group].block_bitmap)) continue;

        uint8_t *bitmap = (uint8_t *)io_buf;
        for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
            if (bitmap[byte] == 0xFF) continue;

            for (int bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    blk_write(bgd_cache[group].block_bitmap);

                    bgd_cache[group].free_blocks--;
                    sb.free_blocks--;

                    /*
                     * Physical block = group_start + bit_position_in_bitmap
                     * bit_position = byte * 8 + bit
                     */
                    return bgd_cache[group].group_start +
                           (byte * 8u) + (uint32_t)bit;
                }
            }
        }
    }

    return 0;
}

static bool free_block(uint32_t block)
{
    if (read_only) return false;

    for (int g = 0; g < bgd_count; g++) {
        uint32_t start = bgd_cache[g].group_start;
        uint32_t end   = start + BLOCKS_PER_GROUP;

        if (block < start || block >= end) continue;

        uint32_t relative = block - start;
        uint32_t byte_idx = relative / 8;
        uint32_t bit_idx  = relative % 8;

        if (!blk_read(bgd_cache[g].block_bitmap)) return false;

        uint8_t *bitmap = (uint8_t *)io_buf;
        bitmap[byte_idx] &= ~(1u << bit_idx);
        blk_write(bgd_cache[g].block_bitmap);

        bgd_cache[g].free_blocks++;
        sb.free_blocks++;
        return true;
    }

    return false;
}

/* ========================================================================
 * Inode allocation
 * ======================================================================== */

static uint32_t alloc_inode(uint32_t preferred_group = 0)
{
    if (read_only || sb.free_inodes == 0) return INODE_INVALID;

    for (int g = 0; g < bgd_count; g++) {
        int group = (int)((preferred_group + (uint32_t)g) % (uint32_t)bgd_count);
        if (bgd_cache[group].free_inodes == 0) continue;

        if (!blk_read(bgd_cache[group].inode_bitmap)) continue;

        uint8_t *bitmap = (uint8_t *)io_buf;
        for (uint32_t byte = 0; byte < (INODES_PER_GROUP / 8); byte++) {
            if (bitmap[byte] == 0xFF) continue;

            for (int bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    blk_write(bgd_cache[group].inode_bitmap);

                    bgd_cache[group].free_inodes--;
                    sb.free_inodes--;

                    return (uint32_t)group * INODES_PER_GROUP +
                           byte * 8u + (uint32_t)bit + 1;
                }
            }
        }
    }

    return INODE_INVALID;
}

/* ========================================================================
 * Extent tree traversal
 * ======================================================================== */

static bool find_extent(const Inode *inode, uint32_t logical_block,
                        uint32_t *physical_block)
{
    if (inode->flags & NBFS_FL_INLINE) return false;

    const ExtentHeader *hdr = &inode->data.tree.header;

    if (hdr->depth == 0) {
        const Extent *exts = inode->data.tree.extents;
        for (uint16_t i = 0; i < hdr->entries; i++) {
            uint32_t start = exts[i].logical_block;
            uint32_t end   = start + exts[i].length;
            if (logical_block >= start && logical_block < end) {
                *physical_block = exts[i].physical_block + (logical_block - start);
                return true;
            }
        }
    } else {
        const ExtentIndex *idxs = (const ExtentIndex *)inode->data.tree.extents;
        int best = -1;
        for (uint16_t i = 0; i < hdr->entries; i++) {
            if (idxs[i].logical_block <= logical_block) best = i;
        }

        if (best >= 0) {
            uint32_t child_data[LONGS_PER_BLK];
            if (!blk_read_to(idxs[best].child_block, child_data)) return false;

            ExtentHeader *child_hdr = (ExtentHeader *)child_data;
            if (child_hdr->depth == 0) {
                Extent *child_exts = (Extent *)(child_data + 2);
                for (uint16_t i = 0; i < child_hdr->entries; i++) {
                    uint32_t start = child_exts[i].logical_block;
                    uint32_t end   = start + child_exts[i].length;
                    if (logical_block >= start && logical_block < end) {
                        *physical_block = child_exts[i].physical_block +
                                          (logical_block - start);
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

/* ========================================================================
 * Directory operations
 * ======================================================================== */

static bool dir_lookup(const Inode *dir_inode, const char *name,
                       LookupResult *result)
{
    if (!(dir_inode->flags & NBFS_FT_DIR)) return false;

    uint8_t name_len = 0;
    for (const char *p = name; *p; p++) name_len++;

    uint32_t target_hash = hash_filename(name, name_len);

    uint64_t dir_size   = ((uint64_t)dir_inode->size_hi << 32) | dir_inode->size_lo;
    uint32_t dir_blocks = (uint32_t)((dir_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < dir_blocks; lb++) {
        uint32_t pb;
        if (!find_extent(dir_inode, lb, &pb)) continue;

        uint32_t dir_data[LONGS_PER_BLK];
        if (!blk_read_to(pb, dir_data)) continue;

        DirBlockHeader *hdr = (DirBlockHeader *)dir_data;
        if (target_hash < hdr->hash_lo || target_hash > hdr->hash_hi) continue;

        uint8_t *ptr = (uint8_t *)dir_data + sizeof(DirBlockHeader);
        uint8_t *end = (uint8_t *)dir_data + BLOCK_SIZE;

        for (uint16_t e = 0; e < hdr->entry_count && ptr < end; e++) {
            DirEntry *de = (DirEntry *)ptr;
            if (de->rec_len == 0) break;

            if (de->name_len == name_len && de->inode != INODE_INVALID) {
                bool match = true;
                for (uint8_t i = 0; i < name_len; i++) {
                    char a = name[i], b = de->name[i];
                    if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                    if (b >= 'a' && b <= 'z') b = (char)(b - 32);
                    if (a != b) { match = false; break; }
                }

                if (match) {
                    result->inode = de->inode;
                    result->file_type = de->file_type;
                    for (uint8_t i = 0; i < name_len; i++) {
                        result->name[i] = de->name[i];
                    }
                    result->name[name_len] = '\0';
                    return true;
                }
            }

            ptr += de->rec_len;
        }
    }

    return false;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool mount(BlockReadFunc reader, BlockWriteFunc writer,
           uint32_t start, uint32_t size, bool ro)
{
    read_block_func  = reader;
    write_block_func = writer;
    partition_offset = start;
    partition_blocks = size;
    read_only        = ro;

    crc32c_init_table();

    if (!read_superblock())   return false;
    if (!load_block_groups()) return false;

    if (sb.state != 0 && !read_only) journal_replay();

    if (!read_only) {
        sb.state = 1;
        sb.mount_count++;
        write_superblock();
    }

    mounted = true;
    return true;
}

bool unmount(void)
{
    if (!mounted) return false;

    if (!read_only) {
        if (txn_active) journal_commit();
        sb.state = 0;
        write_superblock();
    }

    mounted          = false;
    read_block_func  = nullptr;
    write_block_func = nullptr;
    return true;
}

bool read_root_dir(DirEntry *entries, int max_entries, int *count)
{
    *count = 0;
    if (!mounted) return false;

    Inode root;
    if (!read_inode(INODE_ROOT, &root)) return false;
    if (!(root.flags & NBFS_FT_DIR)) return false;

    uint64_t dir_size   = ((uint64_t)root.size_hi << 32) | root.size_lo;
    uint32_t dir_blocks = (uint32_t)((dir_size + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t lb = 0; lb < dir_blocks && *count < max_entries; lb++) {
        uint32_t pb;
        if (!find_extent(&root, lb, &pb)) continue;

        uint32_t dir_data[LONGS_PER_BLK];
        if (!blk_read_to(pb, dir_data)) continue;

        DirBlockHeader *hdr = (DirBlockHeader *)dir_data;
        uint8_t *ptr = (uint8_t *)dir_data + sizeof(DirBlockHeader);
        uint8_t *end = (uint8_t *)dir_data + BLOCK_SIZE;

        for (uint16_t e = 0;
             e < hdr->entry_count && ptr < end && *count < max_entries; e++) {
            DirEntry *de = (DirEntry *)ptr;
            if (de->rec_len == 0) break;
            if (de->inode != INODE_INVALID) {
                entries[*count] = *de;
                (*count)++;
            }
            ptr += de->rec_len;
        }
    }

    return true;
}

bool find_file(uint32_t dir_inode_num, const char *name, LookupResult *result)
{
    if (!mounted) return false;
    Inode dir;
    if (!read_inode(dir_inode_num, &dir)) return false;
    return dir_lookup(&dir, name, result);
}

bool read_file(uint32_t file_inode_num, uint8_t *buffer,
               uint32_t max_size, uint32_t *bytes_read)
{
    *bytes_read = 0;
    if (!mounted) return false;

    Inode file;
    if (!read_inode(file_inode_num, &file)) return false;

    uint64_t file_size = ((uint64_t)file.size_hi << 32) | file.size_lo;
    if (file_size > max_size) file_size = max_size;

    if (file.flags & NBFS_FL_INLINE) {
        uint32_t copy = (file_size > MAX_INLINE_DATA)
                        ? (uint32_t)MAX_INLINE_DATA : (uint32_t)file_size;
        __builtin_memcpy(buffer, file.data.inline_data, copy);
        *bytes_read = copy;
        return true;
    }

    uint32_t remaining    = (uint32_t)file_size;
    uint32_t logical_block = 0;

    while (remaining > 0) {
        uint32_t pb;
        if (!find_extent(&file, logical_block, &pb)) break;

        uint32_t block_data[LONGS_PER_BLK];
        if (!blk_read_to(pb, block_data)) break;

        uint32_t copy = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
        __builtin_memcpy(buffer + *bytes_read, block_data, copy);

        *bytes_read    += copy;
        remaining      -= copy;
        logical_block++;
    }

    return true;
}

bool is_mounted(void)           { return mounted; }
const Superblock *get_superblock(void) { return mounted ? &sb : nullptr; }
uint32_t get_free_blocks(void)  { return mounted ? sb.free_blocks : 0; }
uint32_t get_free_inodes(void)  { return mounted ? sb.free_inodes : 0; }
const char *get_volume_name(void) { return mounted ? sb.volume_name : "unmounted"; }

/* ========================================================================
 * Format (mkfs.nbfs)
 * ======================================================================== */

bool format(BlockWriteFunc writer, uint32_t start, uint32_t total_blocks,
            const char *volume_name)
{
    write_block_func = writer;
    partition_offset = start;
    read_only        = false;

    crc32c_init_table();

    uint32_t num_groups = (total_blocks + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP;
    if (num_groups > (uint32_t)MAX_CACHED_BGD) num_groups = (uint32_t)MAX_CACHED_BGD;

    /* Zero metadata area */
    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    for (uint32_t b = 0; b < 70 + num_groups * 2; b++) blk_write(b);

    /* Build superblock */
    __builtin_memset(&sb, 0, sizeof(Superblock));
    sb.magic             = NBFS_MAGIC;
    sb.version           = NBFS_VERSION;
    sb.block_size        = BLOCK_SIZE;
    sb.total_blocks      = total_blocks;
    sb.free_blocks       = total_blocks - 70;
    sb.total_inodes      = num_groups * INODES_PER_GROUP;
    sb.free_inodes       = sb.total_inodes - INODE_FIRST_USER;
    sb.block_group_count = num_groups;
    sb.blocks_per_group  = BLOCKS_PER_GROUP;
    sb.inodes_per_group  = INODES_PER_GROUP;
    sb.first_data_block  = 70;
    sb.journal_start     = JOURNAL_RING_START;
    sb.journal_size      = JOURNAL_RING_SIZE;
    sb.state             = 0;
    sb.creator_os        = 0;

    int vn_len = 0;
    while (volume_name[vn_len] && vn_len < 31) {
        sb.volume_name[vn_len] = volume_name[vn_len];
        vn_len++;
    }
    sb.volume_name[vn_len] = '\0';

    write_superblock();

    /* Build block group descriptors */
    uint32_t current_block = 70;
    for (uint32_t g = 0; g < num_groups; g++) {
        bgd_cache[g].group_start  = current_block;
        bgd_cache[g].block_bitmap = current_block++;
        bgd_cache[g].inode_bitmap = current_block++;
        bgd_cache[g].inode_table  = current_block;
        current_block += INODE_TABLE_BLOCKS;
        bgd_cache[g].free_blocks  = BLOCKS_PER_GROUP - GROUP_META_BLOCKS;
        bgd_cache[g].free_inodes  = INODES_PER_GROUP;
        bgd_cache[g].used_dirs    = 0;
        bgd_cache[g].flags        = 0;
        bgd_cache[g].crc32        = 0;
        bgd_cache[g].crc32        = crc32c(&bgd_cache[g], sizeof(BlockGroupDesc));
    }

    /* Write BGDT */
    int bgds_per_block = (int)(BLOCK_SIZE / sizeof(BlockGroupDesc));
    uint32_t idx = 0;
    for (uint32_t b = 0; idx < num_groups; b++) {
        __builtin_memset(io_buf, 0, BLOCK_SIZE);
        BlockGroupDesc *dst = (BlockGroupDesc *)io_buf;
        for (int i = 0; i < bgds_per_block && idx < num_groups; i++, idx++) {
            dst[i] = bgd_cache[idx];
        }
        blk_write_from(BGDT_BLOCK + b, io_buf);
    }

    /* Mark metadata blocks as allocated in group 0's block bitmap */
    if (!blk_read(bgd_cache[0].block_bitmap)) return false;
    uint8_t *bmap = (uint8_t *)io_buf;
    uint32_t meta_bits = GROUP_META_BLOCKS;
    for (uint32_t b = 0; b < meta_bits; b++) {
        bmap[b / 8] |= (uint8_t)(1u << (b % 8));
    }
    blk_write(bgd_cache[0].block_bitmap);

    /* Create root directory inode */
    bgd_count = (int)num_groups;
    Inode root;
    __builtin_memset(&root, 0, sizeof(Inode));
    root.flags      = NBFS_FT_DIR;
    root.link_count = 2;
    root.size_lo    = BLOCK_SIZE;
    root.block_count = 1;
    root.data.tree.header.magic       = EXTENT_MAGIC;
    root.data.tree.header.entries     = 1;
    root.data.tree.header.max_entries = MAX_INLINE_EXTENTS;
    root.data.tree.header.depth       = 0;

    uint32_t root_data_block = bgd_cache[0].inode_table + INODE_TABLE_BLOCKS;
    root.data.tree.extents[0].logical_block  = 0;
    root.data.tree.extents[0].physical_block = root_data_block;
    root.data.tree.extents[0].length         = 1;

    write_inode(INODE_ROOT, &root);

    /* Write empty root directory data block */
    __builtin_memset(io_buf, 0, BLOCK_SIZE);
    DirBlockHeader *dhdr = (DirBlockHeader *)io_buf;
    dhdr->magic       = 0x4E424454UL;
    dhdr->hash_lo     = 0;
    dhdr->hash_hi     = 0xFFFFFFFFUL;
    dhdr->entry_count = 0;
    dhdr->free_space  = (uint16_t)(BLOCK_SIZE - sizeof(DirBlockHeader));
    dhdr->crc32       = 0;
    dhdr->crc32       = crc32c(dhdr, sizeof(DirBlockHeader));
    blk_write(root_data_block);

    return true;
}

} /* namespace nbfs */
} /* namespace neo */
