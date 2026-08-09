#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"

#define IMAGE "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int check_lookup(
    nbfs_context_t *ctx,
    const char *name,
    uint64_t expected)
{
    uint64_t inode = 0;

    if (nbfs_lookup(ctx, 1, name, &inode) != 0)
        return fail("lookup failed");

    printf("PASS: lookup \"%s\" -> inode %llu\n",
           name,
           (unsigned long long)inode);

    if (inode != expected)
        return fail("lookup returned wrong inode");

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;

    printf("NBFS directory lookup test\n");
    printf("===========================\n");

    ctx = nbfs_open(IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * The mkfs root directory contains:
     *
     * .  -> inode 1
     * .. -> inode 1
     */
    if (check_lookup(ctx, ".", 1) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    if (check_lookup(ctx, "..", 1) != 0)
    {
        nbfs_close(ctx);
        return 1;
    }

    /*
     * This name must not exist.
     */
    {
        uint64_t inode = 0;

        if (nbfs_lookup(ctx, 1, "does-not-exist", &inode) == 0)
        {
            nbfs_close(ctx);
            return fail("non-existent entry was found");
        }

        printf("PASS: missing entry correctly rejected\n");
    }

    nbfs_close(ctx);

    printf("\nNBFS directory lookup: OK\n");

    return 0;
}
