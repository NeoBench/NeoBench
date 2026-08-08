#!/usr/bin/env bash
set -euo pipefail

PROJECT="NeoBench"

cd "$PROJECT"

echo "======================================="
echo "Creating NeoBench Memory Manager"
echo "======================================="

mkdir -p kernel/mm
mkdir -p include/kernel

###############################################################################
# Physical Memory Manager
###############################################################################

cat > kernel/mm/pmm.c <<'EOF'
#include <kernel/mm.h>

static uint32_t total_memory = 0;
static uint32_t free_memory = 0;

void pmm_init(uint32_t memory_size)
{
    total_memory = memory_size;
    free_memory  = memory_size;
}

void *pmm_alloc(uint32_t bytes)
{
    (void)bytes;
    return 0;
}

void pmm_free(void *ptr)
{
    (void)ptr;
}
EOF

###############################################################################
# Virtual Memory Manager
###############################################################################

cat > kernel/mm/vmm.c <<'EOF'
#include <kernel/mm.h>

void vmm_init(void)
{

}

void map_page(uint32_t virt, uint32_t phys)
{
    (void)virt;
    (void)phys;
}

void unmap_page(uint32_t virt)
{
    (void)virt;
}
EOF

###############################################################################
# Kernel Heap
###############################################################################

cat > kernel/mm/heap.c <<'EOF'
#include <kernel/mm.h>

void heap_init(void)
{

}

void *kmalloc(uint32_t size)
{
    (void)size;
    return 0;
}

void kfree(void *ptr)
{
    (void)ptr;
}
EOF

###############################################################################
# Memory Header
###############################################################################

cat > include/kernel/mm.h <<'EOF'
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
EOF

###############################################################################
# README
###############################################################################

cat > kernel/mm/README.md <<'EOF'
NeoBench Memory Manager

Components

- Physical Memory Manager
- Virtual Memory Manager
- Kernel Heap

Future

- MMU support
- Demand paging
- Copy-on-write
- Shared memory
- Memory protection
EOF

echo
echo "Memory subsystem created."
