#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "libnbfs.h"

#define IMAGE "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t inode2;
    uint64_t inode3;
    uint64_t inode2_again;

    printf("NBFS inode allocator test\n");
    printf("==========================\n");

    ctx = nbfs_open(IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Allocate the first free inode.
     *
     * Root inode 1 already exists, so the allocator
     * should return inode 2.
     */
    if (nbfs_allocate_inode(ctx, &inode2) != 0)
    {
        nbfs_close(ctx);
        return fail("first inode allocation failed");
    }

    printf("PASS: allocated inode %llu\n",
           (unsigned long long)inode2);

    if (inode2 != 2)
    {
        nbfs_close(ctx);
        return fail("first allocated inode was not 2");
    }

    /*
     * Allocate another inode.
     */
    if (nbfs_allocate_inode(ctx, &inode3) != 0)
    {
        nbfs_close(ctx);
        return fail("second inode allocation failed");
    }

    printf("PASS: allocated inode %llu\n",
           (unsigned long long)inode3);

    if (inode3 != 3)
    {
        nbfs_close(ctx);
        return fail("second allocated inode was not 3");
    }

    /*
     * Free inode 2.
     */
    if (nbfs_free_inode(ctx, inode2) != 0)
    {
        nbfs_close(ctx);
        return fail("free inode 2 failed");
    }

    printf("PASS: freed inode %llu\n",
           (unsigned long long)inode2);

    /*
     * The allocator should now reuse inode 2.
     */
    if (nbfs_allocate_inode(ctx, &inode2_again) != 0)
    {
        nbfs_close(ctx);
        return fail("re-allocation failed");
    }

    printf("PASS: reallocated inode %llu\n",
           (unsigned long long)inode2_again);

    if (inode2_again != 2)
    {
        nbfs_close(ctx);
        return fail("freed inode 2 was not reused");
    }

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    printf("\nNBFS inode allocator: OK\n");

    return 0;
}
