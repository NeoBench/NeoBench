/*
 * NeoBench Bare-Metal Amiga Kernel
 * Memory Manager
 *
 * Bitmap-based page allocator (4KB pages) with separate Chip/Fast RAM pools.
 * Slab allocator for small allocations layered on top.
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace mem {

static constexpr uint32 PAGE_SIZE     = 4096;
static constexpr uint32 PAGE_SHIFT    = 12;
static constexpr uint32 BITS_PER_WORD = 32;

static constexpr uint32 VECTOR_RESERVE = 0x400;   /* First 1KB for exception vectors */

static constexpr uint32 MAX_CHIP_RAM   = 0x200000;   /* 2MB */
static constexpr uint32 MAX_FAST_RAM   = 0x10000000; /* INODE_SIZEMB */
static constexpr uint32 MAX_CHIP_PAGES = MAX_CHIP_RAM / PAGE_SIZE;
static constexpr uint32 MAX_FAST_PAGES = MAX_FAST_RAM / PAGE_SIZE;

static constexpr uint32 CHIP_BITMAP_WORDS = (MAX_CHIP_PAGES + BITS_PER_WORD - 1) / BITS_PER_WORD;
static constexpr uint32 FAST_BITMAP_WORDS = (MAX_FAST_PAGES + BITS_PER_WORD - 1) / BITS_PER_WORD;

static uint32 chip_bitmap[CHIP_BITMAP_WORDS];
static uint32 fast_bitmap[FAST_BITMAP_WORDS];

static uint32 chip_base  = 0;
static uint32 chip_top   = 0;
static uint32 chip_pages = 0;
static uint32 chip_free  = 0;

static uint32 fast_base  = 0;
static uint32 fast_top   = 0;
static uint32 fast_pages = 0;
static uint32 fast_free  = 0;

/* ------------------------------------------------------------------ */
/*  Slab allocator                                                     */
/* ------------------------------------------------------------------ */

static constexpr uint32 SLAB_SIZES[]     = { 32, 64, 128, INODE_SIZE, 512, 1024, 2048 };
static constexpr uint32 NUM_SLAB_CLASSES = 7;
static constexpr uint32 SLAB_MAGIC       = 0x534C4142;  /* "SLAB" */

struct SlabFreeNode {
    SlabFreeNode* next;
};

struct SlabHeader {
    uint32        magic;
    uint32        obj_size;
    uint32        num_objects;
    uint32        num_free;
    SlabFreeNode* free_list;
    SlabHeader*   next_slab;
};

struct SlabClass {
    uint32       obj_size;
    SlabHeader*  partial;
    SlabHeader*  full;
    uint32       total_allocs;
};

static SlabClass slab_classes[NUM_SLAB_CLASSES];

/* ------------------------------------------------------------------ */
/*  Bitmap helpers                                                     */
/* ------------------------------------------------------------------ */

static inline void bitmap_set(uint32* bm, uint32 bit)
{
    bm[bit / BITS_PER_WORD] |= (1U << (bit % BITS_PER_WORD));
}

static inline void bitmap_clear(uint32* bm, uint32 bit)
{
    bm[bit / BITS_PER_WORD] &= ~(1U << (bit % BITS_PER_WORD));
}

static inline bool bitmap_test(const uint32* bm, uint32 bit)
{
    return (bm[bit / BITS_PER_WORD] & (1U << (bit % BITS_PER_WORD))) != 0;
}

static int32 bitmap_find_free(const uint32* bm, uint32 num_bits)
{
    uint32 num_words = (num_bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
    for (uint32 w = 0; w < num_words; w++) {
        if (bm[w] != 0xFFFFFFFF) {
            uint32 val = bm[w];
            for (uint32 b = 0; b < BITS_PER_WORD; b++) {
                uint32 idx = w * BITS_PER_WORD + b;
                if (idx >= num_bits) return -1;
                if (!(val & (1U << b))) return static_cast<int32>(idx);
            }
        }
    }
    return -1;
}

static int32 bitmap_find_contiguous(const uint32* bm, uint32 num_bits, uint32 count)
{
    uint32 run_start = 0;
    uint32 run_len   = 0;
    for (uint32 i = 0; i < num_bits; i++) {
        if (!bitmap_test(bm, i)) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len >= count) return static_cast<int32>(run_start);
        } else {
            run_len = 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Page allocator                                                     */
/* ------------------------------------------------------------------ */

static void* alloc_page_from(uint32* bm, uint32 npages, uint32 base, uint32* free_cnt)
{
    int32 idx = bitmap_find_free(bm, npages);
    if (idx < 0) return nullptr;
    bitmap_set(bm, static_cast<uint32>(idx));
    (*free_cnt)--;
    return reinterpret_cast<void*>(base + (static_cast<uint32>(idx) << PAGE_SHIFT));
}

static void* alloc_pages_from(uint32* bm, uint32 npages, uint32 base,
                               uint32* free_cnt, uint32 count)
{
    int32 idx = bitmap_find_contiguous(bm, npages, count);
    if (idx < 0) return nullptr;
    for (uint32 i = 0; i < count; i++)
        bitmap_set(bm, static_cast<uint32>(idx) + i);
    *free_cnt -= count;
    return reinterpret_cast<void*>(base + (static_cast<uint32>(idx) << PAGE_SHIFT));
}

static void free_page_to(uint32* bm, uint32 base, uint32* free_cnt, void* ptr)
{
    uint32 addr = reinterpret_cast<uint32>(ptr);
    uint32 idx  = (addr - base) >> PAGE_SHIFT;
    bitmap_clear(bm, idx);
    (*free_cnt)++;
}

/* ------------------------------------------------------------------ */
/*  Slab allocator internals                                           */
/* ------------------------------------------------------------------ */

static SlabHeader* slab_create(uint32 class_idx)
{
    void* page = alloc_page_from(fast_bitmap, fast_pages, fast_base, &fast_free);
    if (!page)
        page = alloc_page_from(chip_bitmap, chip_pages, chip_base, &chip_free);
    if (!page) return nullptr;

    SlabHeader* hdr     = static_cast<SlabHeader*>(page);
    uint32 obj_size     = slab_classes[class_idx].obj_size;
    uint32 header_size  = (sizeof(SlabHeader) + 31) & ~31u;  /* Align to 32 bytes */
    uint32 usable       = PAGE_SIZE - header_size;
    uint32 num_objects  = usable / obj_size;

    hdr->magic       = SLAB_MAGIC;
    hdr->obj_size    = obj_size;
    hdr->num_objects = num_objects;
    hdr->num_free    = num_objects;
    hdr->next_slab   = nullptr;
    hdr->free_list   = nullptr;

    uint8* obj_start = reinterpret_cast<uint8*>(page) + header_size;
    for (uint32 i = 0; i < num_objects; i++) {
        SlabFreeNode* node = reinterpret_cast<SlabFreeNode*>(obj_start + i * obj_size);
        node->next = hdr->free_list;
        hdr->free_list = node;
    }

    return hdr;
}

static void* slab_alloc(uint32 cls)
{
    SlabClass& sc = slab_classes[cls];

    if (!sc.partial) {
        SlabHeader* ns = slab_create(cls);
        if (!ns) return nullptr;
        ns->next_slab = sc.partial;
        sc.partial = ns;
    }

    SlabHeader* hdr = sc.partial;
    SlabFreeNode* node = hdr->free_list;
    if (!node) return nullptr;

    hdr->free_list = node->next;
    hdr->num_free--;
    sc.total_allocs++;

    if (hdr->num_free == 0) {
        sc.partial = hdr->next_slab;
        hdr->next_slab = sc.full;
        sc.full = hdr;
    }

    return static_cast<void*>(node);
}

static void slab_free(void* ptr)
{
    uint32 page_addr = reinterpret_cast<uint32>(ptr) & ~(PAGE_SIZE - 1);
    SlabHeader* hdr = reinterpret_cast<SlabHeader*>(page_addr);
    if (hdr->magic != SLAB_MAGIC) return;

    uint32 cls = 0;
    for (uint32 i = 0; i < NUM_SLAB_CLASSES; i++) {
        if (slab_classes[i].obj_size == hdr->obj_size) { cls = i; break; }
    }

    SlabClass& sc = slab_classes[cls];
    bool was_full = (hdr->num_free == 0);

    SlabFreeNode* node = static_cast<SlabFreeNode*>(ptr);
    node->next = hdr->free_list;
    hdr->free_list = node;
    hdr->num_free++;

    if (was_full) {
        SlabHeader** pp = &sc.full;
        while (*pp && *pp != hdr) pp = &((*pp)->next_slab);
        if (*pp == hdr) *pp = hdr->next_slab;
        hdr->next_slab = sc.partial;
        sc.partial = hdr;
    }
}

static uint32 slab_class_for_size(uint32 size)
{
    for (uint32 i = 0; i < NUM_SLAB_CLASSES; i++) {
        if (size <= SLAB_SIZES[i]) return i;
    }
    return NUM_SLAB_CLASSES;
}

/* ------------------------------------------------------------------ */
/*  Public interface                                                    */
/* ------------------------------------------------------------------ */

void init()
{
    extern volatile uint32 _g_chip_top;
    extern volatile uint32 _g_fast_base;
    extern volatile uint32 _g_fast_size;
    extern uint32 _kernel_end;

    chip_base = 0;
    chip_top  = _g_chip_top;
    if (chip_top == 0) chip_top = MAX_CHIP_RAM;
    if (chip_top > MAX_CHIP_RAM) chip_top = MAX_CHIP_RAM;

    fast_base = _g_fast_base;
    uint32 fs = _g_fast_size;
    if (fs > MAX_FAST_RAM) fs = MAX_FAST_RAM;
    fast_top = fast_base + fs;

    chip_pages = chip_top / PAGE_SIZE;
    fast_pages = fs / PAGE_SIZE;

    for (uint32 i = 0; i < CHIP_BITMAP_WORDS; i++) chip_bitmap[i] = 0;
    for (uint32 i = 0; i < FAST_BITMAP_WORDS; i++) fast_bitmap[i] = 0;

    chip_free = chip_pages;
    fast_free = fast_pages;

    /* Reserve page 0: vectors + supervisor data */
    bitmap_set(chip_bitmap, 0);
    chip_free--;

    /* Reserve kernel image pages */
    uint32 kend = reinterpret_cast<uint32>(&_kernel_end);
    uint32 kpages = (kend + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32 i = 1; i < kpages && i < chip_pages; i++) {
        if (!bitmap_test(chip_bitmap, i)) {
            bitmap_set(chip_bitmap, i);
            chip_free--;
        }
    }

    if (kend > fast_base && fast_pages > 0) {
        uint32 fk = (kend - fast_base + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint32 i = 0; i < fk && i < fast_pages; i++) {
            bitmap_set(fast_bitmap, i);
            fast_free--;
        }
    }

    /* Initialize slab classes */
    for (uint32 i = 0; i < NUM_SLAB_CLASSES; i++) {
        slab_classes[i].obj_size     = SLAB_SIZES[i];
        slab_classes[i].partial      = nullptr;
        slab_classes[i].full         = nullptr;
        slab_classes[i].total_allocs = 0;
    }
}

void* alloc(uint32 size)
{
    if (size == 0) return nullptr;

    /* Small allocations: slab allocator */
    if (size <= SLAB_SIZES[NUM_SLAB_CLASSES - 1]) {
        uint32 cls = slab_class_for_size(size);
        if (cls < NUM_SLAB_CLASSES) return slab_alloc(cls);
    }

    /* Large: contiguous pages, prefer Fast RAM.
     * We prepend a uint32 page-count header to enable correct multi-page free. */
    uint32 np = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    /* Allocate one extra page to hold the page-count header. */
    uint32 np_total = np + 1;
    void* raw = alloc_pages_from(fast_bitmap, fast_pages, fast_base, &fast_free, np_total);
    if (!raw) raw = alloc_pages_from(chip_bitmap, chip_pages, chip_base, &chip_free, np_total);
    if (!raw) return nullptr;
    *reinterpret_cast<uint32*>(raw) = np_total;   /* store total pages (incl. header page) */
    return reinterpret_cast<uint8*>(raw) + PAGE_SIZE; /* return past header page */
}

void free(void* ptr)
{
    if (!ptr) return;

    uint32 addr = reinterpret_cast<uint32>(ptr);
    uint32 page_addr = addr & ~(PAGE_SIZE - 1);
    SlabHeader* hdr = reinterpret_cast<SlabHeader*>(page_addr);
    if (hdr->magic == SLAB_MAGIC) { slab_free(ptr); return; }

    /* Large allocation: pointer is offset by PAGE_SIZE from the raw header page.
     * Read back the page count stored in the header page and free all pages. */
    void* raw = reinterpret_cast<uint8*>(ptr) - PAGE_SIZE;
    uint32 raw_addr = reinterpret_cast<uint32>(raw);
    uint32 np_total = *reinterpret_cast<uint32*>(raw);
    if (np_total == 0 || np_total > 65536) return;   /* sanity check */

    if (raw_addr >= fast_base && raw_addr < fast_top) {
        uint32 idx = (raw_addr - fast_base) >> PAGE_SHIFT;
        for (uint32 i = 0; i < np_total; i++) {
            bitmap_clear(fast_bitmap, idx + i);
        }
        fast_free += np_total;
    } else if (raw_addr >= chip_base && raw_addr < chip_top) {
        uint32 idx = (raw_addr - chip_base) >> PAGE_SHIFT;
        for (uint32 i = 0; i < np_total; i++) {
            bitmap_clear(chip_bitmap, idx + i);
        }
        chip_free += np_total;
    }
}

void* alloc_chip(uint32 size)
{
    if (size == 0) return nullptr;
    uint32 np = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32 np_total = np + 1;
    void* raw = alloc_pages_from(chip_bitmap, chip_pages, chip_base, &chip_free, np_total);
    if (!raw) return nullptr;
    *reinterpret_cast<uint32*>(raw) = np_total;
    return reinterpret_cast<uint8*>(raw) + PAGE_SIZE;
}

uint32 get_free_mem()
{
    return (chip_free + fast_free) * PAGE_SIZE;
}

uint32 get_free_chip()
{
    return chip_free * PAGE_SIZE;
}

uint32 get_free_fast()
{
    return fast_free * PAGE_SIZE;
}

uint32 get_total_mem()
{
    return (chip_pages + fast_pages) * PAGE_SIZE;
}

}  /* namespace mem */
}  /* namespace neo */

/* Global kmalloc for other kernel subsystems */
extern "C" void* kmalloc(uint32 size, uint32 align)
{
    if (align <= 4) return neo::mem::alloc(size);
    uint32 total = size + align + sizeof(void*);
    void* raw = neo::mem::alloc(total);
    if (!raw) return nullptr;
    uint32 addr = (reinterpret_cast<uint32>(raw) + sizeof(void*) + align - 1) & ~(align - 1);
    reinterpret_cast<void**>(addr)[-1] = raw;
    return reinterpret_cast<void*>(addr);
}
