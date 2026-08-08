#include <stdint.h>
#include <string.h>

#include "libnbfs.h"

int nbfs_allocate_block(nbfs_context_t *ctx, uint64_t *block)
{
    (void)ctx;

    if (!block)
        return -1;

    *block = 0;
    return 0;
}

int nbfs_free_block(nbfs_context_t *ctx, uint64_t block)
{
    (void)ctx;
    (void)block;
    return 0;
}

int nbfs_allocate_inode(nbfs_context_t *ctx, uint64_t *inode)
{
    (void)ctx;

    if (!inode)
        return -1;

    *inode = 0;
    return 0;
}

int nbfs_free_inode(nbfs_context_t *ctx, uint64_t inode)
{
    (void)ctx;
    (void)inode;
    return 0;
}

static int bitmap_test(const uint8_t *bitmap, uint64_t bit)
{
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

static void bitmap_set(uint8_t *bitmap, uint64_t bit)
{
    bitmap[bit / 8] |= (1u << (bit % 8));
}

static void bitmap_clear(uint8_t *bitmap, uint64_t bit)
{
    bitmap[bit / 8] &= ~(1u << (bit % 8));
}

static uint64_t bitmap_find_first_zero(const uint8_t *bitmap, uint64_t bits)
{
    for (uint64_t i = 0; i < bits; i++)
    {
        if (!bitmap_test(bitmap, i))
            return i;
    }

    return UINT64_MAX;
}
