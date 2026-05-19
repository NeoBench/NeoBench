/*
 * NeoBench Bare-Metal Amiga Kernel
 * Amiga Fast File System (FFS/OFS) Reader
 *
 * Minimal read-only implementation supporting OFS and FFS variants.
 *
 * Corrections vs v1.0:
 *
 *  1. FFS DATA BLOCK POINTER TABLE TRAVERSAL WRONG (critical).
 *     In an FFS file header block, the data block pointer table occupies
 *     longwords [6..LONGS_PER_BLK-52] (indices 6 to 75 inclusive).
 *     The pointers are stored in REVERSE ORDER: the pointer to the LAST
 *     data block is at index [LONGS_PER_BLK-51] = [77], and the pointer
 *     to the FIRST data block is at the LOWEST index in the table.
 *     The original traversed by decrementing ptr_idx from LONGS_PER_BLK-51
 *     (index 77) downward, which reads blocks in REVERSE order (last block
 *     first), producing corrupted file data.
 *     Fixed: iterate from the HIGHEST index (which holds the first data
 *     block pointer) DOWN to index 6.  Actually the conventional layout
 *     is: table[LONGS_PER_BLK-51] = first block, ..., table[6] = last.
 *     Wait - let me be precise. Per the AmigaDOS technical reference:
 *       table[LONGS_PER_BLK-51] = block number of sector 0 (FIRST)
 *       table[LONGS_PER_BLK-52] = block number of sector 1
 *       ...
 *       table[6]               = block number of sector N-1 (LAST)
 *     So index LONGS_PER_BLK-51 (= 77) = FIRST data block.
 *     Index 6 = LAST data block.
 *     Iterating by DECREMENTING ptr_idx from 77 reads first block at 77,
 *     second at 76, etc. which IS correct order.
 *     BUT: the original skipped to an extension block when ptr_idx
 *     reached 0, checking saved_header[ptr_idx] == 0. The issue is that
 *     it should stop at index 6 (the table starts at [6], not [0]).
 *     Indices [0..5] are block type, key, seq, first_data, and checksum
 *     fields - NOT data block pointers. Reading them as pointers gives
 *     garbage block numbers. Fixed: stop at index 6, not 0.
 *
 *  2. FILE SIZE FIELD OFFSET WRONG.
 *     The original read file size from block_buf[LONGS_PER_BLK - 47].
 *     Per the Amiga FFS header block layout:
 *       [LONGS_PER_BLK - 47] = byte_size (file size in bytes). CORRECT.
 *     No change needed here; this was correct.
 *
 *  3. OFS FIRST DATA BLOCK POINTER LOCATION WRONG.
 *     The original read the first OFS data block from
 *     block_buf[LONGS_PER_BLK - 51] which is the same as the FFS first
 *     data block pointer. This is incorrect for OFS.
 *     In OFS, the file header block stores the first data block at
 *     index [LONGS_PER_BLK - 51] as well, so this is actually correct.
 *     However the original then followed the OFS linked list via
 *     block_buf[4] (the "next" pointer), which is correct for OFS data
 *     blocks (offset 4 in the OFS data block = next block number).
 *     No change needed.
 *
 *  4. HASH FUNCTION: __builtin_strlen NOT AVAILABLE IN ALL CONTEXTS.
 *     We add a local strlen to avoid relying on builtins.
 *
 *  5. FFS EXTENSION BLOCK HANDLING.
 *     In the original's FFS read loop, after exhausting the pointers in
 *     the current header/extension block, it looked for an extension at
 *     saved_header[LONGS_PER_BLK - 2].  This is correct: the extension
 *     block pointer is at offset -2 from the end.  However the original
 *     checked for ptr_idx == 0 (hit index 0) to trigger the extension
 *     lookup.  This is wrong because: (a) the table starts at index 6,
 *     and (b) if ptr_idx reaches 6 and still has data, we would read
 *     structural fields as block pointers.  Fixed: trigger extension
 *     when ptr_idx < 6 (below the start of the table).
 *
 *  6. CHECKSUM: SUM SHOULD EQUAL ZERO.
 *     The verify_checksum function sums all 128 longwords and checks for
 *     zero.  This is correct for AmigaDOS blocks (the checksum is stored
 *     such that the sum of all longs including the checksum = 0).
 *     No change needed.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace ffs {

/* ========================================================================
 * Constants
 * ======================================================================== */

static constexpr uint32_t BLOCK_SIZE    = 512;
static constexpr uint32_t LONGS_PER_BLK = BLOCK_SIZE / 4;  /* 128 */

/* Block secondary type identifiers */
static constexpr uint32_t T_HEADER      = 2;
static constexpr uint32_t T_DATA        = 8;

static constexpr uint32_t ST_ROOT       = 1;
static constexpr uint32_t ST_USERDIR    = 2;
static constexpr uint32_t ST_FILE       = 0xFFFFFFFDUL;  /* -3 */
static constexpr uint32_t ST_SOFTLINK   = 3;
static constexpr uint32_t ST_HARDLINK   = 0xFFFFFFFCUL;  /* -4 */

static constexpr uint32_t DOSTYPE_OFS      = 0x444F5300UL;
static constexpr uint32_t DOSTYPE_FFS      = 0x444F5301UL;
static constexpr uint32_t DOSTYPE_OFS_INTL = 0x444F5302UL;
static constexpr uint32_t DOSTYPE_FFS_INTL = 0x444F5303UL;
static constexpr uint32_t DOSTYPE_OFS_DC   = 0x444F5304UL;
static constexpr uint32_t DOSTYPE_FFS_DC   = 0x444F5305UL;

static constexpr int HASH_TABLE_SIZE = 72;

/*
 * FFS header block data pointer table:
 *   First data block pointer:  index [LONGS_PER_BLK - 51] = [77]
 *   Last  data block pointer:  index [6]
 *   Table size:                72 entries (77 down to 6 inclusive)
 * Extension block pointer:     index [LONGS_PER_BLK - 2]  = [126]
 */
static constexpr int FFS_TABLE_HI  = (int)LONGS_PER_BLK - 51;  /* 77 */
static constexpr int FFS_TABLE_LO  = 6;                          /* lowest valid */
static constexpr int FFS_EXT_IDX   = (int)LONGS_PER_BLK - 2;    /* 126 */
static constexpr int FFS_SIZE_IDX  = (int)LONGS_PER_BLK - 47;   /* 81 - byte_size */
static constexpr int FFS_NAME_IDX  = (int)LONGS_PER_BLK - 20;   /* 108 - BCPL name */
static constexpr int FFS_PARENT_IDX= (int)LONGS_PER_BLK - 3;    /* 125 - parent */
static constexpr int FFS_HASH_IDX  = (int)LONGS_PER_BLK - 4;    /* 124 - hash chain */
static constexpr int FFS_DATE_DAYS = (int)LONGS_PER_BLK - 23;   /* 105 */
static constexpr int FFS_DATE_MINS = (int)LONGS_PER_BLK - 22;   /* 106 */
static constexpr int FFS_DATE_TICKS= (int)LONGS_PER_BLK - 21;   /* 107 */

/* ========================================================================
 * Types
 * ======================================================================== */

typedef bool (*BlockReadFunc)(uint32_t block, void *buffer);

struct DirEntry {
    char     name[32];
    uint32_t block;
    uint32_t size;
    uint32_t type;
    uint32_t parent;
    uint32_t days, mins, ticks;
};

/* ========================================================================
 * State
 * ======================================================================== */

static BlockReadFunc read_block_func = nullptr;
static uint32_t     partition_start  = 0;
static uint32_t     partition_size   = 0;
static uint32_t     root_block       = 0;
static bool         is_ffs           = false;
static uint32_t     dos_type         = 0;

static uint32_t block_buf[LONGS_PER_BLK] __attribute__((aligned(4)));

/* ========================================================================
 * Helpers
 * ======================================================================== */

static int ffs_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static bool read_block(uint32_t block)
{
    if (!read_block_func) return false;
    return read_block_func(partition_start + block, block_buf);
}

static bool verify_checksum(const uint32_t *data)
{
    uint32_t sum = 0;
    for (int i = 0; i < (int)LONGS_PER_BLK; i++) sum += data[i];
    return (sum == 0);
}

static uint32_t hash_name(const char *name)
{
    uint32_t hash = (uint32_t)ffs_strlen(name);
    for (const char *p = name; *p; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        hash = (hash * 13 + (uint32_t)c) & 0x7FF;
    }
    return hash % (uint32_t)HASH_TABLE_SIZE;
}

/* Extract BCPL name from block (stored at FFS_NAME_IDX offset) */
static void extract_name(const uint32_t *blk, char *out, int out_size)
{
    const uint8_t *name_area = (const uint8_t *)&blk[FFS_NAME_IDX];
    int len = name_area[0];
    if (len >= out_size) len = out_size - 1;
    for (int i = 0; i < len; i++) out[i] = (char)name_area[1 + i];
    out[len] = '\0';
}

/* ========================================================================
 * Public API
 * ======================================================================== */

bool mount(BlockReadFunc reader, uint32_t start, uint32_t size,
           uint32_t root_blk)
{
    read_block_func = reader;
    partition_start = start;
    partition_size  = size;
    root_block      = root_blk;

    if (root_block == 0) {
        root_block = size / 2;  /* Standard: middle of partition */
    }

    /* Read and verify root block */
    if (!read_block(root_block)) return false;
    if (!verify_checksum(block_buf)) return false;
    if (block_buf[0] != T_HEADER) return false;
    if (block_buf[LONGS_PER_BLK - 1] != ST_ROOT) return false;

    /* Read DOS type from boot block */
    if (!read_block(0)) return false;
    dos_type = block_buf[0];

    is_ffs = (dos_type == DOSTYPE_FFS || dos_type == DOSTYPE_FFS_INTL ||
              dos_type == DOSTYPE_FFS_DC);

    return true;
}

bool read_root(DirEntry *entries, int max_entries, int *count)
{
    *count = 0;

    if (!read_block(root_block)) return false;

    /* Hash table: [6..77] */
    for (int i = 0; i < HASH_TABLE_SIZE && *count < max_entries; i++) {
        uint32_t entry_block = block_buf[6 + i];

        while (entry_block != 0 && *count < max_entries) {
            if (!read_block(entry_block)) break;
            if (!verify_checksum(block_buf)) break;

            DirEntry *e = &entries[*count];
            e->block  = entry_block;
            e->type   = block_buf[LONGS_PER_BLK - 1];
            e->size   = (e->type == ST_FILE) ? block_buf[FFS_SIZE_IDX] : 0;
            e->parent = block_buf[FFS_PARENT_IDX];
            e->days   = block_buf[FFS_DATE_DAYS];
            e->mins   = block_buf[FFS_DATE_MINS];
            e->ticks  = block_buf[FFS_DATE_TICKS];

            extract_name(block_buf, e->name, sizeof(e->name));

            uint32_t next = block_buf[FFS_HASH_IDX];
            (*count)++;
            entry_block = next;
        }
    }

    return true;
}

bool find_entry(uint32_t dir_block, const char *name, DirEntry *result)
{
    if (!read_block(dir_block)) return false;
    if (!verify_checksum(block_buf)) return false;

    uint32_t hash        = hash_name(name);
    uint32_t entry_block = block_buf[6 + hash];
    int      name_len    = ffs_strlen(name);

    while (entry_block != 0) {
        if (!read_block(entry_block)) return false;
        if (!verify_checksum(block_buf)) return false;

        const uint8_t *name_area = (const uint8_t *)&block_buf[FFS_NAME_IDX];
        int stored_len = name_area[0];

        if (stored_len == name_len) {
            bool match = true;
            for (int i = 0; i < name_len; i++) {
                char a = name[i];
                char b = (char)name_area[1 + i];
                if (a >= 'a' && a <= 'z') a = (char)(a - 32);
                if (b >= 'a' && b <= 'z') b = (char)(b - 32);
                if (a != b) { match = false; break; }
            }

            if (match) {
                result->block  = entry_block;
                result->type   = block_buf[LONGS_PER_BLK - 1];
                result->size   = (result->type == ST_FILE)
                                 ? block_buf[FFS_SIZE_IDX] : 0;
                result->parent = block_buf[FFS_PARENT_IDX];
                extract_name(block_buf, result->name, sizeof(result->name));
                return true;
            }
        }

        entry_block = block_buf[FFS_HASH_IDX];
    }

    return false;
}

bool read_file(uint32_t file_header_block, uint8_t *buffer,
               uint32_t max_size, uint32_t *bytes_read)
{
    *bytes_read = 0;

    if (!read_block(file_header_block)) return false;
    if (!verify_checksum(block_buf)) return false;
    if (block_buf[LONGS_PER_BLK - 1] != ST_FILE) return false;

    uint32_t file_size = block_buf[FFS_SIZE_IDX];
    if (file_size > max_size) file_size = max_size;

    if (is_ffs) {
        /*
         * FFS data block pointer table.
         *
         * The table occupies indices [FFS_TABLE_HI .. FFS_TABLE_LO]
         * (indices 77 down to 6 inclusive = 72 entries).
         *
         * IMPORTANT: The pointer at index FFS_TABLE_HI (77) is the
         * pointer to the FIRST data block of the file.  The pointer at
         * index 6 is the LAST data block.
         *
         * We iterate by decrementing ptr_idx from FFS_TABLE_HI.
         * When we reach FFS_TABLE_LO - 1 (5) we have consumed the
         * current header/extension block's table and must follow the
         * extension block pointer.
         */
        uint32_t saved[LONGS_PER_BLK];
        for (int i = 0; i < (int)LONGS_PER_BLK; i++) saved[i] = block_buf[i];

        int      ptr_idx  = FFS_TABLE_HI;
        uint32_t remaining = file_size;

        while (remaining > 0) {
            /* Check if we need to move to extension block */
            if (ptr_idx < FFS_TABLE_LO) {
                uint32_t ext = saved[FFS_EXT_IDX];
                if (ext == 0) break;

                if (!read_block(ext)) return false;
                if (!verify_checksum(block_buf)) return false;
                for (int i = 0; i < (int)LONGS_PER_BLK; i++) saved[i] = block_buf[i];
                ptr_idx = FFS_TABLE_HI;
            }

            uint32_t data_blk = saved[ptr_idx];
            ptr_idx--;

            if (data_blk == 0) continue;  /* Sparse block (uncommon) */

            if (!read_block(data_blk)) return false;

            uint32_t copy = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
            const uint8_t *src = (const uint8_t *)block_buf;
            for (uint32_t i = 0; i < copy; i++) {
                buffer[*bytes_read + i] = src[i];
            }

            *bytes_read += copy;
            remaining   -= copy;
        }

    } else {
        /*
         * OFS: each data block has a 24-byte header.
         *   [0] = T_DATA (8)
         *   [1] = header_key (file header block number)
         *   [2] = sequence number (1-based)
         *   [3] = data_size (bytes of data in this block, max 488)
         *   [4] = next_data (next data block, 0 = end)
         *   [5] = checksum
         *   [6..127] = data (488 bytes)
         *
         * The first data block is pointed to by the file header at
         * index [FFS_TABLE_HI] = [77].
         */
        uint32_t data_block = block_buf[FFS_TABLE_HI];
        uint32_t remaining  = file_size;

        while (data_block != 0 && remaining > 0) {
            if (!read_block(data_block)) return false;
            /* OFS blocks also have checksum */
            /* (OFS data block checksum is at [5]; we skip verify for speed) */

            uint32_t data_size = block_buf[3];
            if (data_size > 488)       data_size = 488;
            if (data_size > remaining) data_size = remaining;

            const uint8_t *src = (const uint8_t *)&block_buf[6];
            for (uint32_t i = 0; i < data_size; i++) {
                buffer[*bytes_read + i] = src[i];
            }

            *bytes_read += data_size;
            remaining   -= data_size;
            data_block   = block_buf[4];  /* Next data block */
        }
    }

    return (*bytes_read > 0 || file_size == 0);
}

bool is_mounted(void)      { return read_block_func != nullptr; }
uint32_t get_dos_type(void){ return dos_type; }

} /* namespace ffs */
} /* namespace neo */
