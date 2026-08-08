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
