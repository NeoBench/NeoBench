#ifndef NB_MM_H
#define NB_MM_H

#include <neobench/types.h>

void pmm_init(uint32_t memory_size);

void *pmm_alloc(uint32_t bytes);

void pmm_free(void *ptr);

void vmm_init(void);

void map_page(uint32_t virt,
              uint32_t phys);

void unmap_page(uint32_t virt);

void heap_init(void);

void *kmalloc(uint32_t size);

void kfree(void *ptr);

#endif
