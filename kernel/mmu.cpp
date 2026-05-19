/*
 * NeoBench Bare-Metal Amiga Kernel
 * MMU Initialization and Management
 *
 * Supports 68030, 68040, and 68060 MMU configurations.
 * 68030: TC, TT0, TT1, SRP, CRP registers
 * 68040/060: TC, DTT0, DTT1, ITT0, ITT1, URP, SRP registers
 */

#include "../include/neobench.h"
#include "../include/types.h"

namespace neo {
namespace mmu {

/* Page table constants */
static constexpr uint32 PAGE_SIZE       = 4096;
static constexpr uint32 PAGE_SHIFT      = 12;
static constexpr uint32 PAGE_MASK       = ~(PAGE_SIZE - 1);

/* 68030 descriptor bits */
static constexpr uint32 DESC_DT_INVALID = 0x00;
static constexpr uint32 DESC_DT_PAGE    = 0x01;
static constexpr uint32 DESC_DT_TABLE4  = 0x02;
static constexpr uint32 DESC_DT_TABLE8  = 0x03;

/* 68040/060 descriptor bits */
static constexpr uint32 DESC_040_RESIDENT    = 0x001;
static constexpr uint32 DESC_040_WRITE_PROT  = 0x004;
static constexpr uint32 DESC_040_USED        = 0x008;
static constexpr uint32 DESC_040_MODIFIED    = 0x010;
static constexpr uint32 DESC_040_CM_WT       = 0x000;
static constexpr uint32 DESC_040_CM_CB       = 0x020;
static constexpr uint32 DESC_040_CM_PRECISE  = 0x040;
static constexpr uint32 DESC_040_CM_IMPRECISE= 0x060;
static constexpr uint32 DESC_040_SUPER       = 0x080;
static constexpr uint32 DESC_040_GLOBAL      = 0x400;

/* 68030 TC register bits */
static constexpr uint32 TC_030_ENABLE    = 0x80000000;
static constexpr uint32 TC_030_PS_SHIFT  = 20;
static constexpr uint32 TC_030_TIA_SHIFT = 12;
static constexpr uint32 TC_030_TIB_SHIFT = 8;
static constexpr uint32 TC_030_TIC_SHIFT = 4;
static constexpr uint32 TC_030_TID_SHIFT = 0;

/* 68040/060 TC register bits */
static constexpr uint32 TC_040_ENABLE   = 0x8000;
/* NOTE: TC bit 14 (P): 0 = 4KB pages, 1 = 8KB pages.
 * TC_040_PAGE4K was 0x4000 (bit14=1) which incorrectly selects 8KB pages.
 * It is removed; 4KB pages are the default when bit 14 is clear. */
static constexpr uint32 TC_040_PAGE_4K_ONLY = 0x0000;  /* bit14=0 = 4KB, kept for documentation */

/* Table sizes */
static constexpr uint32 ROOT_ENTRIES    = 128;
static constexpr uint32 PTR_ENTRIES     = 128;
static constexpr uint32 PAGE_ENTRIES    = 64;

/*
 * Page table storage
 * Aligned to 512 bytes for 040/060 compatibility
 */
static uint32 root_table[ROOT_ENTRIES] __attribute__((aligned(512)));
static uint32 ptr_tables[ROOT_ENTRIES][PTR_ENTRIES] __attribute__((aligned(512)));
static uint32 page_tables[ROOT_ENTRIES][PTR_ENTRIES][PAGE_ENTRIES] __attribute__((aligned(INODE_SIZE)));

static uint32 cpu_type = 0;

/* ------------------------------------------------------------------ */
/*  Low-level MMU register access                                      */
/* ------------------------------------------------------------------ */

static inline void set_tc_030(uint32 val)
{
    __asm__ volatile (
        "lea    %0, %%a0\n\t"
        ".long  0xF0104000\n\t"  /* pmove (a0),tc */
        : : "m"(val) : "a0"
    );
}

static inline void set_srp_030(uint64 val)
{
    __asm__ volatile (
        "lea    %0, %%a0\n\t"
        ".long  0xF0104800\n\t"  /* pmove (a0),srp */
        : : "m"(val) : "a0"
    );
}

static inline void set_crp_030(uint64 val)
{
    __asm__ volatile (
        "lea    %0, %%a0\n\t"
        ".long  0xF0104C00\n\t"  /* pmove (a0),crp */
        : : "m"(val) : "a0"
    );
}

static inline void set_tt0_030(uint32 val)
{
    __asm__ volatile (
        "lea    %0, %%a0\n\t"
        ".long  0xF0100800\n\t"  /* pmove (a0),tt0 */
        : : "m"(val) : "a0"
    );
}

static inline void set_tt1_030(uint32 val)
{
    __asm__ volatile (
        "lea    %0, %%a0\n\t"
        ".long  0xF0100C00\n\t"  /* pmove (a0),tt1 */
        : : "m"(val) : "a0"
    );
}

static inline void set_tc_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%tc" : : "d"(val));
}

static inline uint32 get_tc_040()
{
    uint32 val;
    __asm__ volatile ("movec %%tc, %0" : "=d"(val));
    return val;
}

static inline void set_srp_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%srp" : : "d"(val));
}

static inline void set_urp_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%urp" : : "d"(val));
}

static inline void set_dtt0_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%dtt0" : : "d"(val));
}

static inline void set_dtt1_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%dtt1" : : "d"(val));
}

static inline void set_itt0_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%itt0" : : "d"(val));
}

static inline void set_itt1_040(uint32 val)
{
    __asm__ volatile ("movec %0, %%itt1" : : "d"(val));
}

static inline void cpush_all()
{
    __asm__ volatile (".long 0xF4F8" : : : "memory");  /* cpusha bc */
}

/* ------------------------------------------------------------------ */
/*  Helper: ensure table entries exist for an address                  */
/* ------------------------------------------------------------------ */

static void ensure_tables_030(uint32 ri, uint32 pi)
{
    if ((root_table[ri] & 0x03) == DESC_DT_INVALID) {
        root_table[ri] = ((uint32)&ptr_tables[ri][0] & PAGE_MASK) | DESC_DT_TABLE4;
        for (uint32 i = 0; i < PTR_ENTRIES; i++)
            ptr_tables[ri][i] = DESC_DT_INVALID;
    }
    if ((ptr_tables[ri][pi] & 0x03) == DESC_DT_INVALID) {
        ptr_tables[ri][pi] = ((uint32)&page_tables[ri][pi][0] & PAGE_MASK) | DESC_DT_TABLE4;
        for (uint32 k = 0; k < PAGE_ENTRIES; k++)
            page_tables[ri][pi][k] = DESC_DT_INVALID;
    }
}

static void ensure_tables_040(uint32 ri, uint32 pi)
{
    if (!(root_table[ri] & 0x02)) {
        root_table[ri] = ((uint32)&ptr_tables[ri][0] & ~0x1FFu) | 0x02;
        for (uint32 i = 0; i < PTR_ENTRIES; i++)
            ptr_tables[ri][i] = 0;
    }
    if (!(ptr_tables[ri][pi] & 0x02)) {
        ptr_tables[ri][pi] = ((uint32)&page_tables[ri][pi][0] & ~0xFFu) | 0x02;
        for (uint32 k = 0; k < PAGE_ENTRIES; k++)
            page_tables[ri][pi][k] = 0;
    }
}

/* Map a range of pages as identity-mapped */
static void identity_map_range(uint32 base, uint32 size, uint32 flags_030, uint32 flags_040)
{
    uint32 num_pages = size / PAGE_SIZE;
    for (uint32 p = 0; p < num_pages; p++) {
        uint32 addr = base + p * PAGE_SIZE;
        uint32 ri = (addr >> 25) & 0x7F;
        uint32 pi = (addr >> 18) & 0x7F;
        uint32 gi = (addr >> 12) & 0x3F;

        if (cpu_type == 68030) {
            ensure_tables_030(ri, pi);
            page_tables[ri][pi][gi] = (addr & PAGE_MASK) | flags_030 | DESC_DT_PAGE;
        } else {
            ensure_tables_040(ri, pi);
            page_tables[ri][pi][gi] = (addr & PAGE_MASK) | flags_040 | DESC_040_RESIDENT;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  68030-specific MMU setup                                           */
/* ------------------------------------------------------------------ */

static void init_030(uint32 chip_top, uint32 fast_base, uint32 fast_size)
{
    uint32 tc_off = 0;
    set_tc_030(tc_off);
    set_tt0_030(0);
    set_tt1_030(0);

    /* Clear root table */
    for (uint32 i = 0; i < ROOT_ENTRIES; i++)
        root_table[i] = DESC_DT_INVALID;

    /* Identity-map Chip RAM: cacheable */
    identity_map_range(0x000000, chip_top, 0, 0);

    /* Identity-map Custom chips: 0xDFF000, cache inhibited (bit 6 in 030 short descriptor) */
    identity_map_range(0xDFF000, PAGE_SIZE, (1 << 6), 0);

    /* Identity-map CIA: 0xBFE000-0xBFF000, cache inhibited */
    identity_map_range(0xBFE000, PAGE_SIZE * 2, (1 << 6), 0);

    /* Identity-map Fast RAM: cacheable */
    if (fast_size > 0) {
        identity_map_range(fast_base, fast_size, 0, 0);
    }

    /* Set up SRP/CRP (64-bit: upper=limit/flags, lower=table base) */
    uint64 rp_value = ((uint64)0x80000002ULL << 32) |
                      ((uint32)&root_table[0] & PAGE_MASK) | DESC_DT_TABLE4;
    set_srp_030(rp_value);
    set_crp_030(rp_value);

    /* TC: Enable | PS=0xC (4KB) | TIA=7 | TIB=7 | TIC=6 | TID=0 */
    uint32 tc_val = TC_030_ENABLE
                  | (0x0C << TC_030_PS_SHIFT)
                  | (7 << TC_030_TIA_SHIFT)
                  | (7 << TC_030_TIB_SHIFT)
                  | (6 << TC_030_TIC_SHIFT)
                  | (0 << TC_030_TID_SHIFT);
    set_tc_030(tc_val);
}

/* ------------------------------------------------------------------ */
/*  68040/060-specific MMU setup                                       */
/* ------------------------------------------------------------------ */

static void init_040(uint32 chip_top, uint32 fast_base, uint32 fast_size)
{
    set_tc_040(0);
    set_dtt0_040(0);
    set_dtt1_040(0);
    set_itt0_040(0);
    set_itt1_040(0);
    cpush_all();

    /* Clear root table */
    for (uint32 i = 0; i < ROOT_ENTRIES; i++)
        root_table[i] = 0;

    /* Identity-map Chip RAM: copyback cached */
    identity_map_range(0x000000, chip_top, 0, DESC_040_CM_CB);

    /* Identity-map Custom chips: cache inhibited precise */
    identity_map_range(0xDFF000, PAGE_SIZE, 0, DESC_040_CM_PRECISE);

    /* Identity-map CIA: cache inhibited precise */
    identity_map_range(0xBFE000, PAGE_SIZE * 2, 0, DESC_040_CM_PRECISE);

    /* Identity-map Fast RAM: copyback cached */
    if (fast_size > 0) {
        identity_map_range(fast_base, fast_size, 0, DESC_040_CM_CB);
    }

    uint32 root_phys = (uint32)&root_table[0] & ~0x1FFu;
    set_srp_040(root_phys);
    set_urp_040(root_phys);

    /* Enable MMU: 4K pages */
    /* Enable MMU with 4KB pages: TC bit14=0 selects 4KB (default when cleared). */
    set_tc_040(TC_040_ENABLE);
}

/* ------------------------------------------------------------------ */
/*  Public interface                                                    */
/* ------------------------------------------------------------------ */

extern volatile uint32 _cpu_type;
extern volatile uint32 _g_chip_top;
extern volatile uint32 _g_fast_base;
extern volatile uint32 _g_fast_size;

void init()
{
    cpu_type = _cpu_type;

    uint32 chip_top  = _g_chip_top;
    uint32 fast_base_val = _g_fast_base;
    uint32 fast_size_val = _g_fast_size;

    if (chip_top == 0) chip_top = 0x200000;

    if (cpu_type == 68030) {
        init_030(chip_top, fast_base_val, fast_size_val);
    } else {
        init_040(chip_top, fast_base_val, fast_size_val);
    }
}

bool map_page(uint32 virt, uint32 phys, uint32 flags)
{
    uint32 ri = (virt >> 25) & 0x7F;
    uint32 pi = (virt >> 18) & 0x7F;
    uint32 gi = (virt >> 12) & 0x3F;

    if (cpu_type == 68030) {
        ensure_tables_030(ri, pi);
        page_tables[ri][pi][gi] = (phys & PAGE_MASK) | (flags & 0xFC) | DESC_DT_PAGE;
    } else {
        ensure_tables_040(ri, pi);
        page_tables[ri][pi][gi] = (phys & PAGE_MASK) | (flags & 0xFFE) | DESC_040_RESIDENT;
    }

    flush_tlb();
    return true;
}

void unmap_page(uint32 virt)
{
    uint32 ri = (virt >> 25) & 0x7F;
    uint32 pi = (virt >> 18) & 0x7F;
    uint32 gi = (virt >> 12) & 0x3F;

    if (cpu_type == 68030) {
        if ((root_table[ri] & 0x03) != DESC_DT_INVALID &&
            (ptr_tables[ri][pi] & 0x03) != DESC_DT_INVALID) {
            page_tables[ri][pi][gi] = DESC_DT_INVALID;
        }
    } else {
        if ((root_table[ri] & 0x02) && (ptr_tables[ri][pi] & 0x02)) {
            page_tables[ri][pi][gi] = 0;
        }
    }

    flush_tlb();
}

void flush_tlb()
{
    if (cpu_type == 68030) {
        __asm__ volatile (
            ".long 0xF0002400\n\t"  /* pflusha */
            : : : "memory"
        );
    } else {
        __asm__ volatile (
            ".long 0xF5180000\n\t"  /* pflusha (040/060) */
            : : : "memory"
        );
    }
}

}  /* namespace mmu */
}  /* namespace neo */
