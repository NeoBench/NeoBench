#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "libnbfs.h"

#define SOURCE_IMAGE "../../images/test-verified.nbfs"
#define TEST_IMAGE   "../../images/test-directory-delete.nbfs"

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
    uint64_t dir_inode = 0;
    uint64_t found = 0;

    printf("NBFS empty directory deletion test\n");
    printf("==================================\n");

    if (reset_image() != 0)
        return fail("unable to reset test image");

    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("unable to open NBFS image");

    /*
     * Create an empty directory beneath root.
     */
    if (nbfs_create_directory(
            ctx,
            1,
            "docs") != 0)
    {
        nbfs_close(ctx);
        return fail("create docs directory failed");
    }

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &dir_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs failed");
    }

    printf(
        "PASS: created docs -> inode %llu\n",
        (unsigned long long)dir_inode);

    /*
     * "." must resolve to itself.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            ".",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs/. failed");
    }

    if (found != dir_inode)
    {
        nbfs_close(ctx);
        return fail("docs/. points to wrong inode");
    }

    printf(
        "PASS: docs/. -> inode %llu\n",
        (unsigned long long)found);

    /*
     * ".." must resolve to root.
     */
    if (nbfs_lookup(
            ctx,
            dir_inode,
            "..",
            &found) != 0)
    {
        nbfs_close(ctx);
        return fail("lookup docs/.. failed");
    }

    if (found != 1)
    {
        nbfs_close(ctx);
        return fail("docs/.. points to wrong inode");
    }

    printf("PASS: docs/.. -> inode 1\n");

    /*
     * Delete the empty directory.
     */
    if (nbfs_delete_directory(
            ctx,
            dir_inode) != 0)
    {
        nbfs_close(ctx);
        return fail("delete empty directory failed");
    }

    printf(
        "PASS: empty directory deletion returned success\n");

    /*
     * It must no longer be visible from root.
     */
    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted directory is still visible");
    }

    printf(
        "PASS: deleted directory no longer found\n");

    /*
     * Flush and close.
     */
    if (nbfs_flush(ctx) != 0)
    {
        nbfs_close(ctx);
        return fail("flush failed");
    }

    nbfs_close(ctx);

    /*
     * Reopen and verify the deletion persisted.
     */
    ctx = nbfs_open(TEST_IMAGE);

    if (!ctx)
        return fail("reopen failed");

    if (nbfs_lookup(
            ctx,
            1,
            "docs",
            &found) == 0)
    {
        nbfs_close(ctx);
        return fail("deleted directory returned after reopen");
    }

    printf(
        "PASS: directory deletion survives close/reopen\n");

    nbfs_close(ctx);

    printf("\nNBFS empty directory deletion: OK\n");

    return 0;
}
