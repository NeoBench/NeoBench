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
