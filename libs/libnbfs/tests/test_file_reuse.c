#include <stdio.h>
#include <stdint.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-libnbfs.nbfs"

static int fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

static int reset_image(void)
{
    FILE *src;
    FILE *dst;
    unsigned char buffer[8192];
    size_t n;

    src = fopen(SOURCE_IMAGE, "rb");
    if (!src)
        return -1;

    dst = fopen(TEST_IMAGE, "wb");
    if (!dst)
    {
        fclose(src);
        return -1;
    }

    while ((n = fread(buffer, 1, sizeof(buffer), src)) != 0)
    {
        if (fwrite(buffer, 1, n, dst) != n)
        {
            fclose(dst);
            fclose(src);
            return -1;
        }
    }

    fclose(dst);
    fclose(src);

    return 0;
}

int main(void)
{
    nbfs_context_t *ctx;
    uint64_t inode_a = 0;
    uint64_t inode_b = 0;
    uint64_t found = 0;

    printf("NBFS file reuse test\n");
    printf("====================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create the first file.
     */
    if (nbfs_create_file(ctx, 1, "first.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create first.txt failed");
    }

    if (nbfs_lookup(ctx, 1, "first.txt", &inode_a) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup first.txt failed");
    }

    printf(
        "PASS: created first.txt -> inode %llu\n",
        (unsigned long long)inode_a);

    /*
     * Delete it.
     */
    if (nbfs_delete_file(ctx, inode_a) != 0)
    {
        nbfs_close(ctx);
        return fail("delete first.txt failed");
    }

    printf("PASS: deleted first.txt\n");

    /*
     * The old entry must be gone.
     */
    if (nbfs_lookup(ctx, 1, "first.txt", &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted file still found");
    }

    printf("PASS: first.txt no longer found\n");

    /*
     * Create another file. The inode allocator should
     * reclaim inode 2.
     */
    if (nbfs_create_file(ctx, 1, "second.txt") != 0)
    {
        nbfs_close(ctx);
        return fail("create second.txt failed");
    }

    if (nbfs_lookup(ctx, 1, "second.txt", &inode_b) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup second.txt failed");
    }

    printf(
        "PASS: created second.txt -> inode %llu\n",
        (unsigned long long)inode_b);

    if (inode_b != inode_a)
    {
        nbfs_close(ctx);
        return fail("inode was not reclaimed");
    }

    printf("PASS: inode was successfully reused\n");

    /*
     * Verify both special directory entries remain valid.
     */
    if (nbfs_lookup(ctx, 1, ".", &found) != 0 ||
        found != 1)
    {
        nbfs_close(ctx);
        return fail("'.' directory entry corrupted");
    }

    if (nbfs_lookup(ctx, 1, "..", &found) != 0 ||
        found != 1)
    {
        nbfs_close(ctx);
        return fail("'..' directory entry corrupted");
    }

    printf("PASS: '.' and '..' remain valid\n");

    /*
     * Verify the new file is present.
     */
    if (nbfs_lookup(ctx, 1, "second.txt", &found) != 0 ||
        found != inode_b)
    {
        nbfs_close(ctx);
        return fail("reused directory entry invalid");
    }

    printf("PASS: reused directory entry is valid\n");

    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Reopen and verify persistence.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to reopen NBFS image");

    if (nbfs_lookup(ctx, 1, "second.txt", &found) != 0 ||
        found != inode_b)
    {
        nbfs_close(ctx);
        return fail("reused file did not survive reopen");
    }

    printf("PASS: reused file survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS file reuse: OK\n");

    return 0;
}
