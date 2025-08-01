/**
 * NeoBench MMU Implementation
 *
 * This module provides MMU support for the NeoBench ROM with 68060 CPU.
 * The Memory Management Unit (MMU) enables virtual memory mapping,
 * memory protection, and caching control.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// MMU constants
#define PAGE_SIZE_4K     0x00001000
#define PAGE_SIZE_8K     0x00002000
#define PAGE_SIZE_16K    0x00004000
#define PAGE_SIZE_32K    0x00008000
#define PAGE_SIZE_64K    0x00010000
#define PAGE_SIZE_128K   0x00020000
#define PAGE_SIZE_256K   0x00040000
#define PAGE_SIZE_512K   0x00080000
#define PAGE_SIZE_1M     0x00100000
#define PAGE_SIZE_2M     0x00200000
#define PAGE_SIZE_4M     0x00400000
#define PAGE_SIZE_8M     0x00800000

// MMU page descriptors
#define MMU_DESC_INVALID  0x00000000  // Invalid entry
#define MMU_DESC_RESIDENT 0x00000001  // Resident page
#define MMU_DESC_INDIRECT 0x00000002  // Indirect descriptor
#define MMU_DESC_PAGE4    0x00000008  // 4KB page
#define MMU_DESC_PAGE8    0x00000010  // 8KB page

// MMU caching modes
#define CACHE_MODE_NOCACHE       0x00000000  // No caching
#define CACHE_MODE_WRITETHROUGH  0x00000040  // Write-through caching
#define CACHE_MODE_COPYBACK      0x00000080  // Copy-back caching
#define CACHE_MODE_NOCACHE_SERIALIZE 0x000000C0  // No cache, serialize

// MMU protection bits
#define MMU_PROT_GLOBAL  0x00000400  // Global
#define MMU_PROT_SUPER   0x00000100  // Supervisor only
#define MMU_PROT_READ    0x00000200  // Read access only
#define MMU_PROT_WRITE   0x00000000  // Read/write access

// MMU status bits
#define MMU_STATUS_MODIFIED 0x00000004  // Page modified
#define MMU_STATUS_USED     0x00000008  // Page accessed

// Translation table entry structure
typedef struct {
    uint32_t descriptor;   // MMU descriptor word
    uint32_t physical;     // Physical address
    uint32_t attributes;   // Additional attributes
} mmu_entry_t;

// MMU Root Table (top level)
#define MMU_ROOT_TABLE_SIZE 128
static mmu_entry_t mmu_root_table[MMU_ROOT_TABLE_SIZE];

// MMU Pointer Tables (second level) - dynamically allocated
#define MMU_PTR_TABLE_SIZE 128
typedef struct {
    mmu_entry_t entries[MMU_PTR_TABLE_SIZE];
    int used;                 // Number of entries used
} mmu_ptr_table_t;

// MMU Page Tables (third level) - dynamically allocated
#define MMU_PAGE_TABLE_SIZE 64
typedef struct {
    mmu_entry_t entries[MMU_PAGE_TABLE_SIZE];
    int used;                 // Number of entries used
} mmu_page_table_t;

// MMU Control
typedef struct {
    uint32_t enabled;         // MMU enabled flag
    uint32_t urp;             // User root pointer
    uint32_t srp;             // Supervisor root pointer
    uint32_t tc;              // Translation control register
    uint32_t itt0;            // Instruction transparent translation 0
    uint32_t itt1;            // Instruction transparent translation 1
    uint32_t dtt0;            // Data transparent translation 0
    uint32_t dtt1;            // Data transparent translation 1
    uint32_t mmusr;           // MMU status register
    uint32_t page_faults;     // Page fault counter
    uint32_t tlb_misses;      // TLB miss counter
    uint32_t tlb_hits;        // TLB hit counter
} mmu_control_t;

// TLB (Translation Lookaside Buffer) entry
typedef struct {
    uint32_t virtual_addr;    // Virtual address
    uint32_t physical_addr;   // Physical address
    uint32_t attributes;      // Page attributes
    uint8_t  valid;           // Valid entry flag
} tlb_entry_t;

// TLB cache
#define TLB_SIZE 32
static tlb_entry_t tlb_cache[TLB_SIZE];
static int tlb_next_entry = 0;  // Next TLB entry to replace

// Global MMU control structure
static mmu_control_t mmu_control;

// Function prototypes
void mmu_init(void);
void mmu_enable(void);
void mmu_disable(void);
int mmu_add_mapping(uint32_t virtual_addr, uint32_t physical_addr, uint32_t size, uint32_t attributes);
int mmu_remove_mapping(uint32_t virtual_addr);
uint32_t mmu_translate(uint32_t virtual_addr, uint8_t is_write);
void mmu_flush_tlb(void);
void mmu_flush_tlb_entry(uint32_t virtual_addr);
void mmu_invalidate_cache(void);
void mmu_set_tc(uint32_t tc);
void mmu_set_urp(uint32_t urp);
void mmu_set_srp(uint32_t srp);
void mmu_set_transparent_translation(int reg_num, uint32_t value);
int mmu_handle_page_fault(uint32_t virtual_addr, uint8_t is_write);

/**
 * Initialize the MMU
 */
void mmu_init(void) {
    // Clear all tables
    memset(mmu_root_table, 0, sizeof(mmu_root_table));
    memset(tlb_cache, 0, sizeof(tlb_cache));
    
    // Initialize MMU control registers
    memset(&mmu_control, 0, sizeof(mmu_control));
    
    // Setup default values
    mmu_control.enabled = 0;
    mmu_control.urp = (uint32_t)mmu_root_table;
    mmu_control.srp = (uint32_t)mmu_root_table;
    
    // Set up translation control register
    // Page size 4KB, enable table search, 7-bit function code lookup
    mmu_control.tc = 0x00008000;
    
    // Reset counters
    mmu_control.page_faults = 0;
    mmu_control.tlb_misses = 0;
    mmu_control.tlb_hits = 0;
}

/**
 * Enable the MMU
 */
void mmu_enable(void) {
    if (!mmu_control.enabled) {
        mmu_control.enabled = 1;
        mmu_flush_tlb();
    }
}

/**
 * Disable the MMU
 */
void mmu_disable(void) {
    mmu_control.enabled = 0;
}

/**
 * Add a virtual-to-physical memory mapping
 * Returns 0 on success, -1 on failure
 */
int mmu_add_mapping(uint32_t virtual_addr, uint32_t physical_addr, 
                   uint32_t size, uint32_t attributes) {
    if (!size || (virtual_addr & (PAGE_SIZE_4K-1)) || (physical_addr & (PAGE_SIZE_4K-1))) {
        // Invalid parameters - addresses must be page-aligned
        return -1;
    }
    
    // Round up size to nearest page
    size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    
    // Loop through the address range, creating page mappings
    for (uint32_t vaddr = virtual_addr; vaddr < virtual_addr + size; vaddr += PAGE_SIZE_4K) {
        uint32_t paddr = physical_addr + (vaddr - virtual_addr);
        
        // Calculate indices for the tables
        uint32_t root_idx = (vaddr >> 25) & 0x7F;
        uint32_t ptr_idx = (vaddr >> 18) & 0x7F;
        uint32_t page_idx = (vaddr >> 12) & 0x3F;
        
        // Check if pointer table exists
        if ((mmu_root_table[root_idx].descriptor & 3) == 0) {
            // Allocate a new pointer table
            mmu_ptr_table_t *ptr_table = (mmu_ptr_table_t*)malloc(sizeof(mmu_ptr_table_t));
            if (!ptr_table) {
                return -1;  // Out of memory
            }
            
            // Clear the new table
            memset(ptr_table, 0, sizeof(mmu_ptr_table_t));
            
            // Add to root table
            mmu_root_table[root_idx].descriptor = MMU_DESC_INDIRECT | ((uint32_t)ptr_table & 0xFFFFFF00);
            mmu_root_table[root_idx].physical = (uint32_t)ptr_table;
        }
        
        // Get pointer to the pointer table
        mmu_ptr_table_t *ptr_table = (mmu_ptr_table_t *)(mmu_root_table[root_idx].physical);
        
        // Check if page table exists
        if ((ptr_table->entries[ptr_idx].descriptor & 3) == 0) {
            // Allocate a new page table
            mmu_page_table_t *page_table = (mmu_page_table_t*)malloc(sizeof(mmu_page_table_t));
            if (!page_table) {
                return -1;  // Out of memory
            }
            
            // Clear the new table
            memset(page_table, 0, sizeof(mmu_page_table_t));
            
            // Add to pointer table
            ptr_table->entries[ptr_idx].descriptor = MMU_DESC_INDIRECT | ((uint32_t)page_table & 0xFFFFFF00);
            ptr_table->entries[ptr_idx].physical = (uint32_t)page_table;
            ptr_table->used++;
        }
        
        // Get pointer to the page table
        mmu_page_table_t *page_table = (mmu_page_table_t *)(ptr_table->entries[ptr_idx].physical);
        
        // Set up the page descriptor
        page_table->entries[page_idx].descriptor = MMU_DESC_RESIDENT | MMU_DESC_PAGE4 | (attributes & 0xFFFFFF00);
        page_table->entries[page_idx].physical = paddr;
        page_table->entries[page_idx].attributes = attributes;
        page_table->used++;
        
        // Invalidate TLB entry for this address if it exists
        mmu_flush_tlb_entry(vaddr);
    }
    
    return 0;
}

/**
 * Remove a virtual memory mapping
 * Returns 0 on success, -1 if mapping not found
 */
int mmu_remove_mapping(uint32_t virtual_addr) {
    if (virtual_addr & (PAGE_SIZE_4K-1)) {
        // Invalid address - must be page-aligned
        return -1;
    }
    
    // Calculate indices for the tables
    uint32_t root_idx = (virtual_addr >> 25) & 0x7F;
    uint32_t ptr_idx = (virtual_addr >> 18) & 0x7F;
    uint32_t page_idx = (virtual_addr >> 12) & 0x3F;
    
    // Check if pointer table exists
    if ((mmu_root_table[root_idx].descriptor & 3) != MMU_DESC_INDIRECT) {
        return -1;  // Mapping not found
    }
    
    // Get pointer to the pointer table
    mmu_ptr_table_t *ptr_table = (mmu_ptr_table_t *)(mmu_root_table[root_idx].physical);
    
    // Check if page table exists
    if ((ptr_table->entries[ptr_idx].descriptor & 3) != MMU_DESC_INDIRECT) {
        return -1;  // Mapping not found
    }
    
    // Get pointer to the page table
    mmu_page_table_t *page_table = (mmu_page_table_t *)(ptr_table->entries[ptr_idx].physical);
    
    // Check if page mapping exists
    if ((page_table->entries[page_idx].descriptor & 3) != MMU_DESC_RESIDENT) {
        return -1;  // Mapping not found
    }
    
    // Remove the mapping
    page_table->entries[page_idx].descriptor = 0;
    page_table->entries[page_idx].physical = 0;
    page_table->entries[page_idx].attributes = 0;
    page_table->used--;
    
    // Clean up empty tables
    if (page_table->used == 0) {
        // No more entries in this page table, remove it
        ptr_table->entries[ptr_idx].descriptor = 0;
        ptr_table->entries[ptr_idx].physical = 0;
        ptr_table->used--;
        free(page_table);
        
        // If pointer table is empty, remove it too
        if (ptr_table->used == 0) {
            mmu_root_table[root_idx].descriptor = 0;
            mmu_root_table[root_idx].physical = 0;
            free(ptr_table);
        }
    }
    
    // Invalidate TLB entry
    mmu_flush_tlb_entry(virtual_addr);
    
    return 0;
}

/**
 * Translate a virtual address to physical using the MMU
 * Returns the physical address or 0 if translation failed
 * If is_write is set, marks the page as modified
 */
uint32_t mmu_translate(uint32_t virtual_addr, uint8_t is_write) {
    if (!mmu_control.enabled) {
        // MMU disabled, use 1:1 mapping
        return virtual_addr;
    }
    
    // Check TLB cache first
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb_cache[i].valid) {
            // Compare page addresses (ignoring offset)
            if ((tlb_cache[i].virtual_addr & ~(PAGE_SIZE_4K-1)) == (virtual_addr & ~(PAGE_SIZE_4K-1))) {
                // TLB hit
                mmu_control.tlb_hits++;
                
                // Get physical address by adding offset to the base
                uint32_t offset = virtual_addr & (PAGE_SIZE_4K-1);
                uint32_t physical_addr = tlb_cache[i].physical_addr | offset;
                
                // For write access, mark the page as modified in the tables
                if (is_write) {
                    // We'd need to update the actual page table entry here
                    // but for simplicity in this emulation, we'll skip that step
                }
                
                return physical_addr;
            }
        }
    }
    
    // TLB miss, need to do table walk
    mmu_control.tlb_misses++;
    
    // Calculate indices for the tables
    uint32_t root_idx = (virtual_addr >> 25) & 0x7F;
    uint32_t ptr_idx = (virtual_addr >> 18) & 0x7F;
    uint32_t page_idx = (virtual_addr >> 12) & 0x3F;
    uint32_t offset = virtual_addr & 0xFFF;
    
    // Check if root entry exists
    if ((mmu_root_table[root_idx].descriptor & 3) != MMU_DESC_INDIRECT) {
        // No mapping for this address
        mmu_control.page_faults++;
        mmu_handle_page_fault(virtual_addr, is_write);
        return 0;
    }
    
    // Get pointer to the pointer table
    mmu_ptr_table_t *ptr_table = (mmu_ptr_table_t *)(mmu_root_table[root_idx].physical);
    
    // Check if pointer table entry exists
    if ((ptr_table->entries[ptr_idx].descriptor & 3) != MMU_DESC_INDIRECT) {
        // No mapping for this address
        mmu_control.page_faults++;
        mmu_handle_page_fault(virtual_addr, is_write);
        return 0;
    }
    
    // Get pointer to the page table
    mmu_page_table_t *page_table = (mmu_page_table_t *)(ptr_table->entries[ptr_idx].physical);
    
    // Check if page table entry exists
    if ((page_table->entries[page_idx].descriptor & 3) != MMU_DESC_RESIDENT) {
        // No mapping for this address
        mmu_control.page_faults++;
        mmu_handle_page_fault(virtual_addr, is_write);
        return 0;
    }
    
    // Get the physical address
    uint32_t physical_addr = (page_table->entries[page_idx].physical & ~(PAGE_SIZE_4K-1)) | offset;
    
    // Mark the page as accessed
    page_table->entries[page_idx].descriptor |= MMU_STATUS_USED;
    
    // If this is a write, mark the page as modified
    if (is_write) {
        page_table->entries[page_idx].descriptor |= MMU_STATUS_MODIFIED;
    }
    
    // Add to TLB cache
    tlb_cache[tlb_next_entry].virtual_addr = virtual_addr & ~(PAGE_SIZE_4K-1);
    tlb_cache[tlb_next_entry].physical_addr = physical_addr & ~(PAGE_SIZE_4K-1);
    tlb_cache[tlb_next_entry].attributes = page_table->entries[page_idx].attributes;
    tlb_cache[tlb_next_entry].valid = 1;
    
    // Move to next TLB entry for replacement
    tlb_next_entry = (tlb_next_entry + 1) % TLB_SIZE;
    
    return physical_addr;
}

/**
 * Flush the entire TLB
 */
void mmu_flush_tlb(void) {
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb_cache[i].valid = 0;
    }
}

/**
 * Flush a specific TLB entry
 */
void mmu_flush_tlb_entry(uint32_t virtual_addr) {
    // Clear the page address bits
    virtual_addr &= ~(PAGE_SIZE_4K-1);
    
    // Find and invalidate matching TLB entries
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb_cache[i].valid && tlb_cache[i].virtual_addr == virtual_addr) {
            tlb_cache[i].valid = 0;
        }
    }
}

/**
 * Invalidate CPU caches
 */
void mmu_invalidate_cache(void) {
    // This would invalidate both instruction and data caches
    // Since this is an emulation, we'll just flush the TLB
    mmu_flush_tlb();
}

/**
 * Set the Translation Control Register
 */
void mmu_set_tc(uint32_t tc) {
    mmu_control.tc = tc;
}

/**
 * Set the User Root Pointer
 */
void mmu_set_urp(uint32_t urp) {
    mmu_control.urp = urp;
}

/**
 * Set the Supervisor Root Pointer
 */
void mmu_set_srp(uint32_t srp) {
    mmu_control.srp = srp;
}

/**
 * Set a transparent translation register
 */
void mmu_set_transparent_translation(int reg_num, uint32_t value) {
    switch (reg_num) {
        case 0: mmu_control.itt0 = value; break;
        case 1: mmu_control.itt1 = value; break;
        case 2: mmu_control.dtt0 = value; break;
        case 3: mmu_control.dtt1 = value; break;
    }
}

/**
 * Handle a page fault
 * Returns 0 if fault was handled, -1 if it could not be handled
 */
int mmu_handle_page_fault(uint32_t virtual_addr, uint8_t is_write) {
    // In a real implementation, this would try to load the page
    // from disk or allocate a new page, etc.
    
    // For this emulation, we'll just report the fault
    return -1;
}