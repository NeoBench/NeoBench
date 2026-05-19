/*
 * NeoBench Bare-Metal Amiga Kernel
 * Memory Manager (C99 Refactored)
 */

#include "../include/neobench.h"
#include "../include/types.h"
#include <string.h>

#define PAGE_SIZE     4096
#define PAGE_SHIFT    12
#define BITS_PER_WORD 32

#define MAX_CHIP_RAM   0x200000   /* 2MB */
#define MAX_FAST_RAM   0x10000000 /* 256MB */
#define MAX_CHIP_PAGES (MAX_CHIP_RAM / PAGE_SIZE)
#define MAX_FAST_PAGES (MAX_FAST_RAM / PAGE_SIZE)

#define CHIP_BITMAP_WORDS ((MAX_CHIP_PAGES + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define FAST_BITMAP_WORDS ((MAX_FAST_PAGES + BITS_PER_WORD - 1) / BITS_PER_WORD)

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

/* Slab Allocator structures */
#define NUM_SLAB_CLASSES 7
#define SLAB_MAGIC       0x534C4142

typedef struct SlabFreeNode {
    struct SlabFreeNode* next;
} SlabFreeNode;

typedef struct SlabHeader {
    uint32        magic;
    uint32        obj_size;
    uint32        num_objects;
    uint32        num_free;
    SlabFreeNode* free_list;
    struct SlabHeader*   next_slab;
} SlabHeader;

typedef struct {
    uint32       obj_size;
    SlabHeader*  partial;
    SlabHeader*  full;
    uint32       total_allocs;
} SlabClass;

static uint32 SLAB_SIZES[] = { 32, 64, 128, INODE_SIZE, 512, 1024, 2048 };
static SlabClass slab_classes[NUM_SLAB_CLASSES];

/* Bitmap helpers */
static void bitmap_set(uint32* bm, uint32 bit) {
    bm[bit / BITS_PER_WORD] |= (1U << (bit % BITS_PER_WORD));
}

static void bitmap_clear(uint32* bm, uint32 bit) {
    bm[bit / BITS_PER_WORD] &= ~(1U << (bit % BITS_PER_WORD));
}

static bool bitmap_test(const uint32* bm, uint32 bit) {
    return (bm[bit / BITS_PER_WORD] & (1U << (bit % BITS_PER_WORD))) != 0;
}

static int32_t bitmap_find_free(const uint32* bm, uint32 num_bits) {
    uint32 num_words = (num_bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
    for (uint32 w = 0; w < num_words; w++) {
        if (bm[w] != 0xFFFFFFFF) {
            for (uint32 b = 0; b < BITS_PER_WORD; b++) {
                uint32 idx = w * BITS_PER_WORD + b;
                if (idx >= num_bits) return -1;
                if (!(bm[w] & (1U << b))) return (int32_t)idx;
            }
        }
    }
    return -1;
}

/* Internal page allocs */
static void* alloc_page_from(uint32* bm, uint32 npages, uint32 base, uint32* free_cnt) {
    int32_t idx = bitmap_find_free(bm, npages);
    if (idx < 0) return (void*)0;
    bitmap_set(bm, (uint32)idx);
    (*free_cnt)--;
    return (void*)(base + ((uint32)idx << PAGE_SHIFT));
}

/* Public API */
void mem_init(void) {
    extern uint32 _g_chip_top;
    extern uint32 _g_fast_base;
    extern uint32 _g_fast_size;
    extern uint32 _kernel_end;

    chip_base = 0;
    chip_top  = _g_chip_top;
    if (chip_top == 0) chip_top = MAX_CHIP_RAM;

    fast_base = _g_fast_base;
    fast_top  = fast_base + (_g_fast_size > MAX_FAST_RAM ? MAX_FAST_RAM : _g_fast_size);

    chip_pages = chip_top / PAGE_SIZE;
    fast_pages = (_g_fast_size > MAX_FAST_RAM ? MAX_FAST_RAM : _g_fast_size) / PAGE_SIZE;

    memset(chip_bitmap, 0, sizeof(chip_bitmap));
    memset(fast_bitmap, 0, sizeof(fast_bitmap));

    chip_free = chip_pages;
    fast_free = fast_pages;

    bitmap_set(chip_bitmap, 0);
    chip_free--;

    uint32 kend = (uint32)&_kernel_end;
    uint32 kpages = (kend + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32 i = 1; i < kpages && i < chip_pages; i++) {
        bitmap_set(chip_bitmap, i);
        chip_free--;
    }

    for (uint32 i = 0; i < NUM_SLAB_CLASSES; i++) {
        slab_classes[i].obj_size = SLAB_SIZES[i];
        slab_classes[i].partial = (SlabHeader*)0;
        slab_classes[i].full = (SlabHeader*)0;
        slab_classes[i].total_allocs = 0;
    }
}

void* mem_alloc(uint32 size) {
    if (size == 0) return (void*)0;
    /* Large: contiguous pages (minimal implementation) */
    uint32 np = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return alloc_page_from(fast_bitmap, fast_pages, fast_base, &fast_free);
}

void mem_free(void* ptr) {
    /* Minimal implementation */
}
